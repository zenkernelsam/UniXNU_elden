"""Regression for anonymous APFS owners hidden by macOS stat projection."""
import struct
import unittest
from seed_ownership import fletcher, inode_owners, verify_owners


def leaf(child_owner=(99, 99)):
    block = bytearray(4096)
    struct.pack_into('<II', block, 24, 0x40000003, 14)
    struct.pack_into('<HHIHH', block, 32, 2, 0, 2, 0, 16)
    for i, (inode, owner, mode) in enumerate(((2, (501, 20), 0o40755), (17, child_owner, 0o100644))):
        vo = (i+1)*92
        struct.pack_into('<4H', block, 56+i*8, i*8, 8, vo, 92)
        struct.pack_into('<Q', block, 72+i*8, (3 << 60) | inode)
        struct.pack_into('<IIH', block, 4096-vo+72, *owner, mode)
    struct.pack_into('<Q', block, 0, fletcher(block))
    return bytes(block)


class OwnershipRegression(unittest.TestCase):
    profile = {'entries': {'child': {}}, 'raw_ownership': {'root': [501, 20], 'children': [99, 99]}}
    live = {'.': 2, 'child': 17}

    def test_anonymous_child_is_accepted(self):
        self.assertEqual(verify_owners(inode_owners([leaf()]), self.live, self.profile), 2)

    def test_projected_host_owner_is_rejected_despite_valid_checksum(self):
        with self.assertRaisesRegex(ValueError, 'Raw APFS owner mismatch: child'):
            verify_owners(inode_owners([leaf((501, 20))]), self.live, self.profile)

    def test_conflicting_retained_versions_are_rejected(self):
        with self.assertRaises(ValueError):
            verify_owners(inode_owners([leaf(), leaf((501, 20))]), self.live, self.profile)

    def test_corrupt_node_cannot_supply_ownership_evidence(self):
        block = bytearray(leaf());block[300] ^= 1
        with self.assertRaises(ValueError):
            verify_owners(inode_owners([bytes(block)]), self.live, self.profile)

    def test_duplicate_live_inodes_cannot_hide_a_wrong_owner(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            verify_owners(inode_owners([leaf()]), {'.': 2, 'child': 2}, self.profile)


if __name__ == '__main__':
    unittest.main()
