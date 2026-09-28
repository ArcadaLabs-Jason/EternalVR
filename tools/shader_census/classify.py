"""Per-module stereo classification (DECISIONS T-049, T-050, T-051, T-052, T-053, T-070, T-105).

Without a view-constant member list, "reads view constants" means "reads a 4x4 / 4x3 float matrix (or
an array of 3-4 vec4) from a uniform, storage or push-constant block": an upper bound, since object,
bone and light matrices look the same. With the list from a live value match (T-050), a read counts
only when it overlaps a listed (set, binding, offset) member.

A module needs a *semantic patch* (T-105) when it needs anything beyond the gl_Position rewrite and
mechanical image-array promotion:
  - a non-position stage (fragment, compute, task, tess control) reads view constants;
  - gl_FragCoord feeds a bin lookup (a buffer index or a texel-buffer fetch, T-051);
  - a position stage writes a vec4 output other than gl_Position from a matrix product (a
    previous-clip or other clip-space varying, T-051).
Screen-texture fetches by gl_FragCoord are counted but are not semantic: promotion adds the layer.
"""
import spirv as S

POSITION_STAGES = {'vertex', 'tess_eval', 'geometry', 'mesh'}
MATRIX_BYTES = 64


class ViewMembers:
    """View-constant members from a live match: [{"set":s,"binding":b,"offset":o,"size":64}, ...];
    "storage":"push" entries match push constants by offset alone."""

    def __init__(self, entries):
        self.entries = [(e.get('storage'), e.get('set'), e.get('binding'), e['offset'],
                         e.get('size', MATRIX_BYTES)) for e in entries]

    def matches(self, storage, set_, binding, offset, size):
        if offset is None:
            return False
        for e_storage, e_set, e_binding, e_offset, e_size in self.entries:
            if e_storage == 'push':
                if storage != 'push':
                    continue
            elif (e_set, e_binding) != (set_, binding):
                continue
            if offset < e_offset + e_size and e_offset < offset + size:
                return True
        return False


def _is_position_pointer(m, root, path):
    if m.builtin(root) == S.BUILTIN_POSITION:
        return True
    type_id = m.variables[root][0]
    for index in path[:2]:  # gl_PerVertex, possibly behind a per-vertex array (tess, geometry, mesh)
        t = m.types.get(type_id)
        if not t or index is None:
            return False
        if t[0] == S.OP_TYPE_STRUCT:
            return index < len(t) - 1 and m.member_builtin(type_id, index) == S.BUILTIN_POSITION
        if t[0] in (S.OP_TYPE_ARRAY, S.OP_TYPE_RUNTIME_ARRAY):
            type_id = t[1]
    return False


def _matrix_reads(m, chains, view_members):
    """Loads of matrix-like block members: (load result ids, [read descriptions])."""
    loads, reads, seen = set(), [], set()
    for opcode, ops, _ in m.code:
        if opcode != S.OP_LOAD or ops[2] not in chains:
            continue
        root, path = chains[ops[2]]
        pointee, storage = m.variables[root]
        if storage not in S.BUFFER_CLASSES:
            continue
        set_, binding = m.binding(root)
        steps = list(m.walk(root, path))
        found = None
        for type_id, offset in steps:
            kind = m.matrix_like(type_id)
            if kind:
                found = (kind, offset)
                break
        if not found:  # a whole struct loaded: every matrix member it holds counts
            last_type, last_offset = steps[-1] if steps else (None, None)
            t = m.types.get(last_type)
            if t and t[0] == S.OP_TYPE_STRUCT:
                for i, member in enumerate(t[1:]):
                    kind = m.matrix_like(member)
                    off = m.member_offset(last_type, i)
                    if kind:
                        found = (kind, None if last_offset is None or off is None else last_offset + off)
                        break
        if not found:
            continue
        kind, offset = found
        name = S.STORAGE_NAMES[storage]
        if view_members is not None and not view_members.matches(name, set_, binding, offset, MATRIX_BYTES):
            continue
        loads.add(ops[1])
        key = (name, set_, binding, offset, kind)
        if key not in seen:
            seen.add(key)
            reads.append({'storage': name, 'set': set_, 'binding': binding, 'offset': offset, 'kind': kind})
    return loads, reads


def _fragcoord(m, chains):
    sources = {ops[1] for opcode, ops, _ in m.code
               if opcode == S.OP_LOAD and ops[2] in chains
               and m.builtin(chains[ops[2]][0]) == S.BUILTIN_FRAG_COORD}
    out = {'uses': len(sources), 'screen_fetch': False, 'bin_lookup': False}
    if not sources:
        return out
    tainted = m.taint(sources)
    for opcode, ops, _ in m.code:
        if opcode in S.IMAGE_COORD_OPS and ops[3] in tainted:
            dim = m.image_dim(m.result_types.get(ops[2]))
            if dim and dim[0] == S.DIM_BUFFER:
                out['bin_lookup'] = True
            else:
                out['screen_fetch'] = True
        elif opcode == S.OP_IMAGE_WRITE and ops[1] in tainted:
            out['screen_fetch'] = True
        elif opcode in S.ACCESS_CHAINS and ops[2] in chains:
            root = chains[ops[2]][0]
            if m.variables[root][1] in S.BUFFER_CLASSES and any(i in tainted for i in ops[3:]):
                out['bin_lookup'] = True
    return out


def _clip_outputs(m, chains):
    """vec4 outputs other than gl_Position written from a matrix-vector product, and whether
    gl_Position itself is."""
    products = {ops[1] for opcode, ops, _ in m.code
                if opcode in (S.OP_VECTOR_TIMES_MATRIX, S.OP_MATRIX_TIMES_VECTOR)
                and m.is_float_vec(ops[0], 4)}
    tainted = m.taint(products) if products else set()
    outputs, position = set(), False
    for opcode, ops, _ in m.code:
        if opcode != S.OP_STORE or ops[1] not in tainted or ops[0] not in chains:
            continue
        root, path = chains[ops[0]]
        if m.variables[root][1] != S.SC_OUTPUT:
            continue
        if _is_position_pointer(m, root, path):
            position = True
        elif m.builtin(root) is None and m.is_float_vec(m.variables[root][0], 4):
            outputs.add(root)
    return sorted(outputs), position


def _images(m):
    out = {'sampled_2d': 0, 'storage_2d': 0, 'arrayed_2d': 0, 'runtime_arrays': 0, 'descriptors': []}
    for var, (pointee, storage) in sorted(m.variables.items()):
        if storage != S.SC_UNIFORM_CONSTANT:
            continue
        dim = m.image_dim(pointee)
        if not dim:
            continue
        d, arrayed, sampled = dim
        runtime = m.kind(pointee) == S.OP_TYPE_RUNTIME_ARRAY
        set_, binding = m.binding(var)
        out['descriptors'].append({'set': set_, 'binding': binding, 'dim': d, 'arrayed': bool(arrayed),
                                   'storage': sampled == 2, 'runtime_array': runtime})
        out['runtime_arrays'] += runtime
        if d == S.DIM_2D:
            if arrayed:
                out['arrayed_2d'] += 1
            elif sampled == 2:
                out['storage_2d'] += 1
            else:
                out['sampled_2d'] += 1
    return out


def classify(data, view_members=None):
    """Classifies one SPIR-V module (bytes). Returns a JSON-ready dict."""
    m = S.Module(data)
    chains = m.chains()
    stages = sorted({model for model, _, _ in m.entry_points})
    view_loads, view_reads = _matrix_reads(m, chains, view_members)
    frag = _fragcoord(m, chains)
    clip_outputs, position_from_product = _clip_outputs(m, chains)
    writes_position = any(opcode == S.OP_STORE and ops[0] in chains and
                          m.variables[chains[ops[0]][0]][1] == S.SC_OUTPUT and
                          _is_position_pointer(m, *chains[ops[0]]) for opcode, ops, _ in m.code)
    position_from_view = False
    if view_loads and writes_position:
        tainted = m.taint(view_loads)
        position_from_view = any(opcode == S.OP_STORE and ops[1] in tainted and ops[0] in chains and
                                 _is_position_pointer(m, *chains[ops[0]]) for opcode, ops, _ in m.code)
    global_id = any(m.builtin(v) == S.BUILTIN_GLOBAL_INVOCATION_ID for v in m.variables)
    position_stage = bool(set(stages) & POSITION_STAGES)

    classes, reasons = [], []
    if position_from_view:
        classes.append('position_patch')
    if view_reads and not position_stage:
        reasons.append('non-position stage reads view constants')
    if frag['bin_lookup']:
        reasons.append('gl_FragCoord bin lookup')
    if frag['screen_fetch']:
        classes.append('fragcoord_screen_fetch')
    if position_stage and clip_outputs:
        reasons.append('clip-space varying besides gl_Position (previous-clip candidate)')
    if 'compute' in stages and global_id:
        classes.append('compute_global_id')
    images = _images(m)
    if images['sampled_2d'] or images['storage_2d']:
        classes.append('images_to_promote')
    return {
        'stages': stages,
        'entry_points': [name for _, _, name in m.entry_points],
        'writes_position': writes_position,
        'position_from_matrix_product': position_from_product,
        'position_from_view_constants': position_from_view,
        'view_constant_reads': view_reads,
        'view_constant_rule': 'member list' if view_members is not None else 'any matrix (upper bound)',
        'fragcoord': frag,
        'global_invocation_id': global_id,
        'clip_like_outputs': len(clip_outputs),
        'images': images,
        'classes': classes,
        'semantic_reasons': reasons,
        'needs_semantic_patch': bool(reasons),
    }
