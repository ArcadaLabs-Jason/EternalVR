"""Minimal SPIR-V reader for the stereo census: types, decorations, variables, access-chain paths and a
forward data-flow ("taint") pass. Pure Python, no SDK needed.

Only what the census uses is modelled. Ids an instruction reads are found by opcode layout for the
opcodes listed below; anything else neither propagates nor sinks taint.
"""
import struct

MAGIC = 0x07230203

# Opcodes
OP_NAME, OP_MEMBER_NAME, OP_EXT_INST, OP_ENTRY_POINT = 5, 6, 12, 15
OP_TYPE_VOID, OP_TYPE_BOOL, OP_TYPE_INT, OP_TYPE_FLOAT = 19, 20, 21, 22
OP_TYPE_VECTOR, OP_TYPE_MATRIX, OP_TYPE_IMAGE, OP_TYPE_SAMPLER = 23, 24, 25, 26
OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT = 27, 28, 29, 30
OP_TYPE_POINTER, OP_TYPE_FUNCTION = 32, 33
OP_CONSTANT, OP_SPEC_CONSTANT = 43, 50
OP_FUNCTION, OP_FUNCTION_PARAMETER, OP_FUNCTION_END, OP_FUNCTION_CALL = 54, 55, 56, 57
OP_VARIABLE, OP_LOAD, OP_STORE = 59, 61, 62
OP_ACCESS_CHAIN, OP_IN_BOUNDS_ACCESS_CHAIN, OP_PTR_ACCESS_CHAIN, OP_IN_BOUNDS_PTR_ACCESS_CHAIN = 65, 66, 67, 70
OP_DECORATE, OP_MEMBER_DECORATE = 71, 72
OP_IMAGE_WRITE = 99
OP_VECTOR_TIMES_MATRIX, OP_MATRIX_TIMES_VECTOR, OP_MATRIX_TIMES_MATRIX = 144, 145, 146
OP_SHIFT_RIGHT_LOGICAL, OP_SHIFT_RIGHT_ARITHMETIC = 194, 195
OP_UDIV, OP_SDIV = 134, 135
OP_RETURN_VALUE = 254

ACCESS_CHAINS = {OP_ACCESS_CHAIN, OP_IN_BOUNDS_ACCESS_CHAIN, OP_PTR_ACCESS_CHAIN, OP_IN_BOUNDS_PTR_ACCESS_CHAIN}
# Image reads whose operands are (type, result, image, coordinate, ...).
IMAGE_COORD_OPS = set(range(87, 99))

# Opcodes laid out as (result type, result id, id operands...). Literal operands among the rest (for
# example OpCompositeExtract indices) are harmless for taint: they only matter if they equal a tainted id.
VALUE_OPS = ({OP_LOAD, 60, 68, 77, 78, 79, 80, 81, 82, 83, 84, 86, 100, 101, 102, 103, 104, 105, 106, 107, 169}
             | set(range(87, 99)) | set(range(109, 125)) | set(range(126, 153)) | set(range(154, 161))
             | set(range(164, 192)) | set(range(194, 203)) | set(range(207, 216)) | set(range(227, 243)))
OP_PHI = 245

# Decorations
DEC_BLOCK, DEC_BUFFER_BLOCK, DEC_ARRAY_STRIDE, DEC_BUILTIN = 2, 3, 6, 11
DEC_LOCATION, DEC_BINDING, DEC_DESCRIPTOR_SET, DEC_OFFSET = 30, 33, 34, 35

# BuiltIns
BUILTIN_POSITION, BUILTIN_FRAG_COORD, BUILTIN_GLOBAL_INVOCATION_ID = 0, 15, 28

# Storage classes
SC_UNIFORM_CONSTANT, SC_INPUT, SC_UNIFORM, SC_OUTPUT = 0, 1, 2, 3
SC_PUSH_CONSTANT, SC_STORAGE_BUFFER, SC_PHYSICAL_STORAGE_BUFFER = 9, 12, 5349
BUFFER_CLASSES = {SC_UNIFORM, SC_PUSH_CONSTANT, SC_STORAGE_BUFFER, SC_PHYSICAL_STORAGE_BUFFER}
STORAGE_NAMES = {SC_UNIFORM: 'uniform', SC_PUSH_CONSTANT: 'push', SC_STORAGE_BUFFER: 'storage',
                 SC_PHYSICAL_STORAGE_BUFFER: 'physical'}

EXECUTION_MODELS = {0: 'vertex', 1: 'tess_control', 2: 'tess_eval', 3: 'geometry', 4: 'fragment',
                    5: 'compute', 5267: 'task', 5268: 'mesh', 5364: 'task', 5365: 'mesh'}

DIM_2D, DIM_BUFFER = 1, 5


def _string(words):
    raw = b''.join(struct.pack('<I', w) for w in words)
    text = raw.split(b'\0', 1)[0]
    return text.decode('utf-8', 'replace'), (len(text) // 4) + 1


class Module:
    """A parsed SPIR-V module."""

    def __init__(self, data):
        if len(data) < 20 or len(data) % 4:
            raise ValueError('not a SPIR-V module (size)')
        words = list(struct.unpack('<%dI' % (len(data) // 4), data))
        if words[0] != MAGIC:
            swapped = list(struct.unpack('>%dI' % (len(data) // 4), data))
            if swapped[0] != MAGIC:
                raise ValueError('not a SPIR-V module (magic)')
            words = swapped
        self.version = words[1]
        self.bound = words[3]
        self.entry_points = []      # (model name, function id, name)
        self.names = {}
        self.decorations = {}       # id -> {decoration: [literals]}
        self.member_decorations = {}  # (struct id, member) -> {decoration: [literals]}
        self.types = {}             # id -> tuple
        self.constants = {}         # id -> first literal word
        self.variables = {}         # id -> (pointee type id, storage class)
        self.result_types = {}      # result id -> type id
        self.code = []              # (opcode, operands, function id) inside functions
        self.function_params = {}   # function id -> [parameter ids]
        self._parse(words)

    def _parse(self, words):
        pos = 5
        function = None
        while pos < len(words):
            count, opcode = words[pos] >> 16, words[pos] & 0xFFFF
            if count == 0:
                raise ValueError('bad instruction at word %d' % pos)
            ops = words[pos + 1:pos + count]
            pos += count
            if opcode == OP_NAME:
                self.names[ops[0]] = _string(ops[1:])[0]
            elif opcode == OP_ENTRY_POINT:
                name, _ = _string(ops[2:])
                self.entry_points.append((EXECUTION_MODELS.get(ops[0], 'other'), ops[1], name))
            elif opcode == OP_DECORATE:
                self.decorations.setdefault(ops[0], {})[ops[1]] = list(ops[2:])
            elif opcode == OP_MEMBER_DECORATE:
                self.member_decorations.setdefault((ops[0], ops[1]), {})[ops[2]] = list(ops[3:])
            elif OP_TYPE_VOID <= opcode <= OP_TYPE_FUNCTION:
                self.types[ops[0]] = (opcode,) + tuple(ops[1:])
            elif opcode in (OP_CONSTANT, OP_SPEC_CONSTANT):
                self.result_types[ops[1]] = ops[0]
                self.constants[ops[1]] = ops[2] if len(ops) > 2 else 0
            elif opcode == OP_VARIABLE:
                self.result_types[ops[1]] = ops[0]
                pointer = self.types.get(ops[0])
                self.variables[ops[1]] = (pointer[2] if pointer else None, ops[2])
                if function is not None:
                    self.code.append((opcode, ops, function))
            elif opcode == OP_FUNCTION:
                function = ops[1]
                self.function_params[function] = []
            elif opcode == OP_FUNCTION_END:
                function = None
            else:
                if opcode == OP_FUNCTION_PARAMETER and function is not None:
                    self.function_params[function].append(ops[1])
                if opcode in VALUE_OPS or opcode in ACCESS_CHAINS or opcode in (OP_FUNCTION_CALL, OP_EXT_INST,
                                                                                 OP_PHI, OP_FUNCTION_PARAMETER):
                    self.result_types[ops[1]] = ops[0]
                if function is not None:
                    self.code.append((opcode, ops, function))

    # --- types -------------------------------------------------------------------------------------

    def kind(self, type_id):
        t = self.types.get(type_id)
        return t[0] if t else None

    def is_float_vec(self, type_id, n):
        t = self.types.get(type_id)
        return bool(t and t[0] == OP_TYPE_VECTOR and t[2] == n and self.kind(t[1]) == OP_TYPE_FLOAT)

    def matrix_like(self, type_id):
        """'mat4', 'mat3x4' style names for 4x4/4x3 float matrices and arrays of 3-4 vec4; else None."""
        t = self.types.get(type_id)
        if not t:
            return None
        if t[0] == OP_TYPE_MATRIX and t[2] in (3, 4):
            col = self.types.get(t[1])
            if col and col[0] == OP_TYPE_VECTOR and col[2] in (3, 4) and self.kind(col[1]) == OP_TYPE_FLOAT:
                return 'mat%dx%d' % (t[2], col[2])
        if t[0] == OP_TYPE_ARRAY and self.is_float_vec(t[1], 4) and self.constants.get(t[2]) in (3, 4):
            return 'vec4[%d]' % self.constants[t[2]]
        return None

    def member_offset(self, struct_id, member):
        return self.member_decorations.get((struct_id, member), {}).get(DEC_OFFSET, [None])[0]

    def builtin(self, var_id):
        return self.decorations.get(var_id, {}).get(DEC_BUILTIN, [None])[0]

    def member_builtin(self, struct_id, member):
        return self.member_decorations.get((struct_id, member), {}).get(DEC_BUILTIN, [None])[0]

    def binding(self, var_id):
        d = self.decorations.get(var_id, {})
        return d.get(DEC_DESCRIPTOR_SET, [None])[0], d.get(DEC_BINDING, [None])[0]

    def image_dim(self, type_id):
        """(dim, arrayed, sampled) of an image, sampled image, or arrays of them; None otherwise."""
        seen = 0
        while type_id in self.types and seen < 8:
            seen += 1
            t = self.types[type_id]
            if t[0] == OP_TYPE_IMAGE:
                return t[2], t[4], t[6]
            if t[0] in (OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_POINTER):
                type_id = t[2] if t[0] == OP_TYPE_POINTER else t[1]
            else:
                return None
        return None

    # --- pointers ----------------------------------------------------------------------------------

    def chains(self):
        """Resolves every access chain: id -> (root variable, [constant index or None, ...])."""
        out = {v: (v, []) for v in self.variables}
        for opcode, ops, _ in self.code:
            if opcode in ACCESS_CHAINS and ops[2] in out:
                root, path = out[ops[2]]
                start = 4 if opcode in (OP_PTR_ACCESS_CHAIN, OP_IN_BOUNDS_PTR_ACCESS_CHAIN) else 3
                idx = [self.constants.get(i) if i in self.constants else None for i in ops[start:]]
                out[ops[1]] = (root, path + idx)
        return out

    def walk(self, root, path):
        """Follows a constant path from a variable: yields (type id, byte offset or None) per step,
        starting with the variable's own pointee type at offset 0."""
        type_id = self.variables[root][0]
        offset = 0
        yield type_id, offset
        for index in path:
            t = self.types.get(type_id)
            if not t:
                return
            if t[0] == OP_TYPE_STRUCT:
                if index is None or index >= len(t) - 1:
                    return
                member_offset = self.member_offset(type_id, index)
                offset = None if offset is None or member_offset is None else offset + member_offset
                type_id = t[1 + index]
            elif t[0] in (OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY):
                stride = self.decorations.get(type_id, {}).get(DEC_ARRAY_STRIDE, [None])[0]
                if index is None or stride is None or offset is None:
                    offset = None
                else:
                    offset += index * stride
                type_id = t[1]
            elif t[0] in (OP_TYPE_MATRIX, OP_TYPE_VECTOR):
                type_id = t[1]
                offset = offset  # inside a matrix: report the matrix's own offset
            else:
                return
            yield type_id, offset

    # --- data flow ---------------------------------------------------------------------------------

    def taint(self, sources):
        """Forward data flow from `sources` (result ids). Returns the tainted ids, including variables
        and pointers stored to and the parameters and results of called functions."""
        tainted = set(sources)
        roots = {k: v[0] for k, v in self.chains().items()}
        returns = {}
        for opcode, ops, fn in self.code:
            if opcode == OP_RETURN_VALUE:
                returns.setdefault(fn, []).append(ops[0])
        changed = True
        rounds = 0
        while changed and rounds < 64:
            changed = False
            rounds += 1
            for opcode, ops, fn in self.code:
                new = set()
                if opcode == OP_STORE:
                    if ops[1] in tainted:
                        new.update((ops[0], roots.get(ops[0], ops[0])))
                elif opcode == OP_LOAD:
                    if ops[2] in tainted or roots.get(ops[2]) in tainted:
                        new.add(ops[1])
                elif opcode == OP_FUNCTION_CALL:
                    params = self.function_params.get(ops[2], [])
                    for i, arg in enumerate(ops[3:]):
                        if (arg in tainted or roots.get(arg) in tainted) and i < len(params):
                            new.add(params[i])
                    if any(r in tainted for r in returns.get(ops[2], [])):
                        new.add(ops[1])
                elif opcode == OP_EXT_INST:
                    if any(a in tainted for a in ops[4:]):
                        new.add(ops[1])
                elif opcode == OP_PHI:
                    if any(a in tainted for a in ops[2::2]):
                        new.add(ops[1])
                elif opcode in VALUE_OPS or opcode in ACCESS_CHAINS:
                    if any(a in tainted for a in ops[2:]):
                        new.add(ops[1])
                new -= tainted
                if new:
                    tainted |= new
                    changed = True
        return tainted
