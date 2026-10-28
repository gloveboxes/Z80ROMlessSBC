"""Independent interval model for the bank-1 extent ABI (test-only)."""
class ExtentModel:
    def __init__(self):
        self.reset()

    def reset(self):
        self.extents = [(8, 247, True)]
        return 1

    def call(self, op, value=0):
        if op == 4:
            return self.reset()
        if op == 3:
            return sum(n for _, n, free in self.extents if free) * 256
        if op == 0:
            if not 0 < value <= 63232:
                return 0
            need = (value + 255) // 256
            for i, (start, count, free) in enumerate(self.extents):
                if free and count >= need:
                    replacement = [(start, need, False)]
                    if count > need:
                        replacement.append((start + need, count - need, True))
                    self.extents[i:i + 1] = replacement
                    return start * 256
            return 0
        if op == 1 and value == 0:
            return 1
        for i, (start, count, free) in enumerate(self.extents):
            if value == start * 256 and not free:
                if op == 2:
                    return count * 256
                if op != 1:
                    return 0
                self.extents[i] = (start, count, True)
                if i + 1 < len(self.extents) and self.extents[i + 1][2]:
                    count += self.extents[i + 1][1]
                    self.extents[i:i + 2] = [(start, count, True)]
                if i and self.extents[i - 1][2]:
                    previous, length, _ = self.extents[i - 1]
                    self.extents[i - 1:i + 1] = [(previous, length + count, True)]
                return 1
        return 0

    def write_tables(self, bank):
        bank[0x100:0x500] = bytes(0x400)
        free_starts = [start for start, _, free in self.extents if free]
        bank[0x10] = free_starts[0] if free_starts else 0
        bank[0x14:0x16] = self.call(3).to_bytes(2, 'little')
        previous = 0
        for start, count, free in self.extents:
            bank[0x100 + start] = count
            bank[0x200 + start] = previous
            if free:
                index = free_starts.index(start)
                bank[0x300 + start] = free_starts[index + 1] if index + 1 < len(free_starts) else 0
                bank[0x400 + start] = free_starts[index - 1] if index else 0
            else:
                bank[0x300 + start] = 255
            previous = start
