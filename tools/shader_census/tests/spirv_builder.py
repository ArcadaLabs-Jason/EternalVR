"""Hand-assembles small SPIR-V modules for the census tests (no SDK needed)."""
import struct

import spirv as S

OP_CAPABILITY, OP_MEMORY_MODEL, OP_EXECUTION_MODE = 17, 14, 16
OP_LABEL, OP_RETURN = 248, 253
OP_CONVERT_F_TO_U, OP_COMPOSITE_EXTRACT, OP_IMAGE_FETCH = 109, 81, 95
MODELS = {'vertex': 0, 'fragment': 4, 'compute': 5}


def _string(text):
    raw = text.encode() + b'\0'
    raw += b'\0' * (-len(raw) % 4)
    return list(struct.unpack('<%dI' % (len(raw) // 4), raw))


class Builder:
    def __init__(self):
        self.bound = 1
        self.entry, self.modes, self.debug, self.dec, self.types, self.body = [], [], [], [], [], []
        self.interface = []
        self.capabilities = [1]  # Shader
        self.extensions = []
        self._cache = {}
        self.void = self.type(S.OP_TYPE_VOID)
        self.float = self.type(S.OP_TYPE_FLOAT, 32)
        self.uint = self.type(S.OP_TYPE_INT, 32, 0)
        self.int = self.type(S.OP_TYPE_INT, 32, 1)

    def id(self):
        self.bound += 1
        return self.bound - 1

    @staticmethod
    def emit(section, op, *operands):
        section.append([((len(operands) + 1) << 16) | op] + list(operands))

    def type(self, op, *operands):
        key = (op,) + operands
        if key not in self._cache:
            i = self.id()
            self.emit(self.types, op, i, *operands)
            self._cache[key] = i
        return self._cache[key]

    def const(self, value, type_id=None):
        type_id = type_id or self.int
        key = ('const', type_id, value)
        if key not in self._cache:
            i = self.id()
            self.emit(self.types, S.OP_CONSTANT, type_id, i, value)
            self._cache[key] = i
        return self._cache[key]

    def vec(self, n):
        return self.type(S.OP_TYPE_VECTOR, self.float, n)

    def uvec(self, n):
        return self.type(S.OP_TYPE_VECTOR, self.uint, n)

    def mat4(self):
        return self.type(S.OP_TYPE_MATRIX, self.vec(4), 4)

    def ptr(self, storage, type_id):
        return self.type(S.OP_TYPE_POINTER, storage, type_id)

    def decorate(self, target, decoration, *literals):
        self.emit(self.dec, S.OP_DECORATE, target, decoration, *literals)

    def struct(self, members, offsets=None, block=True):
        i = self.id()
        self.emit(self.types, S.OP_TYPE_STRUCT, i, *members)
        if offsets is not None:
            for n, off in enumerate(offsets):
                self.emit(self.dec, S.OP_MEMBER_DECORATE, i, n, S.DEC_OFFSET, off)
                vec4 = self._cache.get((S.OP_TYPE_VECTOR, self.float, 4))
                if members[n] == self._cache.get((S.OP_TYPE_MATRIX, vec4, 4)):
                    self.emit(self.dec, S.OP_MEMBER_DECORATE, i, n, 5)  # ColMajor
                    self.emit(self.dec, S.OP_MEMBER_DECORATE, i, n, 7, 16)  # MatrixStride
        if block:
            self.decorate(i, S.DEC_BLOCK)
        return i

    def runtime_array(self, element, stride):
        i = self.id()
        self.emit(self.types, S.OP_TYPE_RUNTIME_ARRAY, i, element)
        self.decorate(i, S.DEC_ARRAY_STRIDE, stride)
        return i

    def image(self, dim=1, sampled=1, arrayed=0):
        return self.type(S.OP_TYPE_IMAGE, self.float, dim, 0, arrayed, 0, sampled, 0)

    def var(self, storage, type_id, set_=None, binding=None, builtin=None, location=None):
        i = self.id()
        self.emit(self.types, S.OP_VARIABLE, self.ptr(storage, type_id), i, storage)
        if set_ is not None:
            self.decorate(i, S.DEC_DESCRIPTOR_SET, set_)
            self.decorate(i, S.DEC_BINDING, binding)
        if builtin is not None:
            self.decorate(i, S.DEC_BUILTIN, builtin)
        if location is not None:
            self.decorate(i, S.DEC_LOCATION, location)
        if storage in (S.SC_INPUT, S.SC_OUTPUT):
            self.interface.append(i)
        return i

    # --- function body --------------------------------------------------------------------------

    def op(self, opcode, result_type, *operands):
        i = self.id()
        self.emit(self.body, opcode, result_type, i, *operands)
        return i

    def load(self, type_id, pointer):
        return self.op(S.OP_LOAD, type_id, pointer)

    def chain(self, storage, type_id, base, *indices):
        return self.op(S.OP_ACCESS_CHAIN, self.ptr(storage, type_id), base, *[self.const(n) for n in indices])

    def store(self, pointer, value):
        self.emit(self.body, S.OP_STORE, pointer, value)

    def local(self, type_id):
        i = self.id()
        self.emit(self.body, S.OP_VARIABLE, self.ptr(7, type_id), i, 7)
        return i

    def build(self, model='vertex', swap=False):
        fn_type = self.type(S.OP_TYPE_FUNCTION, self.void)
        main = self.id()
        label = self.id()
        head = [[(5 << 16) | S.OP_FUNCTION, self.void, main, 0, fn_type], [(2 << 16) | OP_LABEL, label]]
        # Function-local variables must come first in the entry block.
        locals_ = [w for w in self.body if w[0] & 0xFFFF == S.OP_VARIABLE]
        rest = [w for w in self.body if w[0] & 0xFFFF != S.OP_VARIABLE]
        tail = [[(1 << 16) | OP_RETURN], [(1 << 16) | S.OP_FUNCTION_END]]
        name = _string('main')
        entry = [[((3 + len(name) + len(self.interface)) << 16) | S.OP_ENTRY_POINT, MODELS[model], main] + name +
                 self.interface]
        modes = []
        if model == 'fragment':
            modes.append([(3 << 16) | OP_EXECUTION_MODE, main, 7])  # OriginUpperLeft
        elif model == 'compute':
            modes.append([(6 << 16) | OP_EXECUTION_MODE, main, 17, 8, 8, 1])  # LocalSize
        caps = [[(2 << 16) | OP_CAPABILITY, c] for c in self.capabilities]
        for ext in self.extensions:
            text = _string(ext)
            caps.append([((1 + len(text)) << 16) | 10] + text)  # OpExtension
        memory = [[(3 << 16) | OP_MEMORY_MODEL, 0, 1]]
        words = [S.MAGIC, 0x00010300, 0, self.bound, 0]
        for inst in caps + memory + entry + modes + self.dec + self.types + head + locals_ + rest + tail:
            words += inst
        return struct.pack(('>' if swap else '<') + '%dI' % len(words), *words)
