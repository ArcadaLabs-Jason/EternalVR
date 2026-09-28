"""Synthetic shaders for the census tests, one per class the census must tell apart."""
import spirv as S
from tests.spirv_builder import OP_COMPOSITE_EXTRACT, OP_CONVERT_F_TO_U, OP_IMAGE_FETCH, Builder

OP_FUNCTION_CALL = 57


def _view_block(b):
    """UBO at set 0 binding 1: { mat4 viewProj @0; mat4 prevViewProj @64; vec4 misc @128 }."""
    mat, vec4 = b.mat4(), b.vec(4)
    block = b.struct([mat, mat, vec4], [0, 64, 128])
    return b.var(S.SC_UNIFORM, block, set_=0, binding=1), mat, vec4


def vertex(prev_clip=False, per_vertex_block=False, via_local=False):
    """gl_Position = viewProj * pos; optionally a previous-clip output and gl_PerVertex style."""
    b = Builder()
    ubo, mat, vec4 = _view_block(b)
    pos_in = b.var(S.SC_INPUT, vec4, location=0)
    if per_vertex_block:
        per_vertex = b.struct([vec4, b.float], block=True)
        b.emit(b.dec, S.OP_MEMBER_DECORATE, per_vertex, 0, S.DEC_BUILTIN, S.BUILTIN_POSITION)
        b.emit(b.dec, S.OP_MEMBER_DECORATE, per_vertex, 1, S.DEC_BUILTIN, 1)
        out_block = b.var(S.SC_OUTPUT, per_vertex)
    else:
        position = b.var(S.SC_OUTPUT, vec4, builtin=S.BUILTIN_POSITION)
    prev_out = b.var(S.SC_OUTPUT, vec4, location=0) if prev_clip else None
    local = b.local(vec4) if via_local else None

    pos = b.load(vec4, pos_in)
    m = b.load(mat, b.chain(S.SC_UNIFORM, mat, ubo, 0))
    clip = b.op(S.OP_MATRIX_TIMES_VECTOR, vec4, m, pos)
    if via_local:
        b.store(local, clip)
        clip = b.load(vec4, local)
    target = b.chain(S.SC_OUTPUT, vec4, out_block, 0) if per_vertex_block else position
    b.store(target, clip)
    if prev_clip:
        pm = b.load(mat, b.chain(S.SC_UNIFORM, mat, ubo, 1))
        b.store(prev_out, b.op(S.OP_MATRIX_TIMES_VECTOR, vec4, pm, pos))
    return b.build('vertex')


def vertex_world_only():
    """Writes gl_Position straight from the input: reads no matrix."""
    b = Builder()
    vec4 = b.vec(4)
    pos_in = b.var(S.SC_INPUT, vec4, location=0)
    position = b.var(S.SC_OUTPUT, vec4, builtin=S.BUILTIN_POSITION)
    b.store(position, b.load(vec4, pos_in))
    return b.build('vertex')


def fragment_reads_matrix(member=0):
    """Fragment shader that loads a matrix of the view block (e.g. to reconstruct a position)."""
    b = Builder()
    ubo, mat, vec4 = _view_block(b)
    color = b.var(S.SC_OUTPUT, vec4, location=0)
    m = b.load(mat, b.chain(S.SC_UNIFORM, mat, ubo, member))
    col = b.op(OP_COMPOSITE_EXTRACT, vec4, m, 0)
    b.store(color, col)
    return b.build('fragment')


def fragment_bin_lookup():
    """Light bins: uint tile = uint(gl_FragCoord.x) >> 4; read bins[tile]."""
    b = Builder()
    vec4 = b.vec(4)
    frag = b.var(S.SC_INPUT, vec4, builtin=S.BUILTIN_FRAG_COORD)
    color = b.var(S.SC_OUTPUT, vec4, location=0)
    bins = b.runtime_array(b.uint, 4)
    block = b.struct([bins], [0])
    ssbo = b.var(S.SC_STORAGE_BUFFER, block, set_=0, binding=2)
    fc = b.load(vec4, frag)
    x = b.op(OP_COMPOSITE_EXTRACT, b.float, fc, 0)
    ux = b.op(OP_CONVERT_F_TO_U, b.uint, x)
    tile = b.op(S.OP_SHIFT_RIGHT_LOGICAL, b.uint, ux, b.const(4, b.uint))
    ptr = b.op(S.OP_ACCESS_CHAIN, b.ptr(S.SC_STORAGE_BUFFER, b.uint), ssbo, b.const(0), tile)
    b.load(b.uint, ptr)
    b.store(color, fc)
    return b.build('fragment')


def fragment_screen_fetch():
    """texelFetch(sceneColor, ivec2(gl_FragCoord.xy), 0) from a 2D image."""
    b = Builder()
    vec4 = b.vec(4)
    frag = b.var(S.SC_INPUT, vec4, builtin=S.BUILTIN_FRAG_COORD)
    color = b.var(S.SC_OUTPUT, vec4, location=0)
    image_type = b.image(dim=1, sampled=1)
    image = b.var(S.SC_UNIFORM_CONSTANT, image_type, set_=1, binding=0)
    ivec2 = b.type(S.OP_TYPE_VECTOR, b.int, 2)
    vec2 = b.vec(2)
    fc = b.load(vec4, frag)
    xy = b.op(79, vec2, fc, fc, 0, 1)  # OpVectorShuffle
    coord = b.op(110, ivec2, xy)  # OpConvertFToS
    img = b.load(image_type, image)
    b.store(color, b.op(OP_IMAGE_FETCH, vec4, img, coord))
    return b.build('fragment')


def compute_screen():
    """Compute over a 2D storage image indexed by gl_GlobalInvocationID, bindless sampled images."""
    b = Builder()
    uvec3 = b.uvec(3)
    gid = b.var(S.SC_INPUT, uvec3, builtin=S.BUILTIN_GLOBAL_INVOCATION_ID)
    storage_image = b.var(S.SC_UNIFORM_CONSTANT, b.image(dim=1, sampled=2), set_=0, binding=0)
    textures = b.id()
    b.emit(b.types, S.OP_TYPE_RUNTIME_ARRAY, textures, b.image(dim=1, sampled=1))
    bindless = b.var(S.SC_UNIFORM_CONSTANT, textures, set_=0, binding=1)
    b.capabilities.append(5302)  # RuntimeDescriptorArray
    b.extensions.append('SPV_EXT_descriptor_indexing')
    assert storage_image and bindless
    b.load(uvec3, gid)
    return b.build('compute')
