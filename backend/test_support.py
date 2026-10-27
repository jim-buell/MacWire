"""Independent PackBits decoder used to verify encoded bitmap output."""
class MacPaintFile:
    def _unpack_bits(self, packed):
        output = bytearray()
        offset = 0
        while offset < len(packed):
            control = packed[offset]
            offset += 1
            if control <= 127:
                length = control + 1
                output.extend(packed[offset:offset + length])
                offset += length
            elif control >= 129:
                output.extend(packed[offset:offset + 1] * (257 - control))
                offset += 1
        return bytes(output)
