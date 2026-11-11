"""Read-only FAT parser retained from the audited Elden boot-candidate tool."""
import struct

class Fat:
    def __init__(self, data):
        self.data = data
        self.bps = struct.unpack_from('<H', data, 11)[0]
        self.spc = data[13]
        self.reserved = struct.unpack_from('<H', data, 14)[0]
        self.fats = data[16]
        self.root_entries = struct.unpack_from('<H', data, 17)[0]
        smallfat = struct.unpack_from('<H', data, 22)[0]
        self.bits = 16 if smallfat else 32
        self.fat_sectors = smallfat or struct.unpack_from('<I', data, 36)[0]
        self.root_sectors = (self.root_entries*32+self.bps-1)//self.bps
        self.root_start = (self.reserved+self.fats*self.fat_sectors)*self.bps
        self.first_data = self.root_start+self.root_sectors*self.bps
        self.cluster_bytes = self.bps*self.spc
        self.root_cluster = 0 if self.bits == 16 else struct.unpack_from('<I', data, 44)[0]

    def chain(self, cluster):
        chunks, seen = [], set()
        eof = 0xfff8 if self.bits == 16 else 0x0ffffff8
        while cluster < eof:
            assert cluster >= 2 and cluster not in seen
            seen.add(cluster)
            start = self.first_data+(cluster-2)*self.cluster_bytes
            assert start+self.cluster_bytes <= len(self.data)
            chunks.append(self.data[start:start+self.cluster_bytes])
            pos = self.reserved*self.bps+cluster*(self.bits//8)
            cluster = int.from_bytes(self.data[pos:pos+self.bits//8], 'little')
            if self.bits == 32:
                cluster &= 0x0fffffff
        return b''.join(chunks)

    def directory(self, cluster):
        raw = self.chain(cluster) if cluster else self.data[self.root_start:self.first_data]
        names, result = {}, {}
        for offset in range(0, len(raw), 32):
            entry = raw[offset:offset+32]
            if entry[0] == 0:
                break
            if entry[0] == 0xe5:
                names = {}; continue
            if entry[11] == 0x0f:
                if entry[0] & 0x40:
                    names = {}
                names[entry[0]&0x1f] = entry[1:11]+entry[14:26]+entry[28:32]
                continue
            short = entry[:8].decode('ascii').rstrip()+('.'+entry[8:11].decode('ascii').rstrip() if entry[8:11].strip() else '')
            name = b''.join(names[i] for i in sorted(names)).decode('utf-16-le').split('\0')[0].rstrip('\uffff') if names else short
            names = {}
            if entry[11] & 8:
                continue
            first = struct.unpack_from('<H', entry, 26)[0]
            if self.bits == 32:
                first |= struct.unpack_from('<H', entry, 20)[0] << 16
            result[name.casefold()] = (first, struct.unpack_from('<I', entry, 28)[0], entry[11])
        return result

    def read(self, path):
        cluster = self.root_cluster
        parts = path.strip('/').split('/')
        for index, name in enumerate(parts):
            cluster, size, flags = self.directory(cluster)[name.casefold()]
            if index != len(parts)-1:
                assert flags & 0x10
        assert not flags & 0x10
        result = self.chain(cluster)[:size]
        assert len(result) == size
        return result
