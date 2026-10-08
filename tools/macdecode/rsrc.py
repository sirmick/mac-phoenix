"""Read resource forks of the files on a boot disk image.

Uses hfsutils (hmount/hls/hcopy -m) with a private HOME so its
"current volume" state doesn't touch the user's. Files are cached as
MacBinary next to the snapshot so later runs don't re-extract.
"""
import hashlib
import os
import struct
import subprocess
import tempfile
from pathlib import Path

# Folders whose files can put code into the System heap or trap tables.
SYSTEM_DIRS = [b"System Folder", b"System Folder:Extensions", b"System Folder:Control Panels"]


try:    # Apple's dcmp 0/1/2 decompressors (pip install rsrcfork; optional)
    from rsrcfork import compress as _compress
    from rsrcfork.compress import dcmp0 as _dcmp0
    import dcmp0_ext as _dcmp0_ext       # adds extended codes 0x01, 0x05
    _dcmp0.decompress_stream_inner = _dcmp0_ext.decompress_stream_inner
except ImportError:
    _compress = None

COMPRESSED_MAGIC = b"\xa8\x9f\x65\x72"


def decompress(data):
    """A compressed resource ($A89F6572 header) as loaded into RAM, or the
    bytes unchanged if it isn't compressed or we can't decompress it (no
    rsrcfork, or a dcmp other than 0/1/2)."""
    if _compress is None or data[:4] != COMPRESSED_MAGIC:
        return data
    try:
        return _compress.decompress(data)
    except Exception:
        return data


def parse_resource_fork(data):
    """{(type, id): (name, bytes)} from a raw resource fork."""
    if len(data) < 16:
        return {}
    data_off, map_off, data_len, map_len = struct.unpack_from(">IIII", data, 0)
    m = data[map_off:map_off + map_len]
    if len(m) < 30:
        return {}
    tl_off, nl_off = struct.unpack_from(">HH", m, 24)
    ntypes = struct.unpack_from(">H", m, tl_off)[0] + 1 & 0xFFFF
    out = {}
    for i in range(ntypes):
        te = tl_off + 2 + 8 * i
        rtype = m[te:te + 4].decode("mac_roman")
        count, ref_off = struct.unpack_from(">HH", m, te + 4)
        for j in range(count + 1):
            r = tl_off + ref_off + 12 * j
            rid, name_off = struct.unpack_from(">hH", m, r)
            doff = struct.unpack_from(">I", m, r + 4)[0] & 0xFFFFFF
            name = None
            if name_off != 0xFFFF:
                n = m[nl_off + name_off]
                name = m[nl_off + name_off + 1:nl_off + name_off + 1 + n].decode("mac_roman")
            p = data_off + doff
            ln = struct.unpack_from(">I", data, p)[0]
            out[(rtype, rid)] = (name, decompress(data[p + 4:p + 4 + ln]))
    return out


def macbinary_forks(blob):
    dlen, rlen = struct.unpack_from(">II", blob, 83)
    d0 = 128
    r0 = d0 + (dlen + 127) // 128 * 128
    return blob[d0:d0 + dlen], blob[r0:r0 + rlen]


class Disk:
    def __init__(self, image, cache_dir):
        self.image = Path(image).expanduser()
        self.cache = Path(cache_dir)
        self.cache.mkdir(parents=True, exist_ok=True)
        self._home = tempfile.mkdtemp(prefix="macdecode-hfs-")
        self._mounted = False

    def _run(self, *args):
        env = dict(os.environ, HOME=self._home)
        return subprocess.run(list(args), env=env, capture_output=True, check=False)

    def _mount(self):
        if not self._mounted:
            r = self._run("hmount", str(self.image))
            if r.returncode:
                raise RuntimeError(f"hmount {self.image}: {r.stderr.decode(errors='replace')}")
            self._mounted = True

    def files(self, folder):
        """Plain files in a folder, as raw Mac names (bytes)."""
        self._mount()
        r = self._run("hls", "-1aF", b":" + folder)
        names = []
        for line in r.stdout.split(b"\n"):
            if line and not line.endswith(b":") and not line.endswith(b"/"):
                names.append(line[:-1] if line[-1:] in b"*" else line)
        return names

    def folders(self, folder):
        """Sub-folders of a folder, as raw Mac names (bytes)."""
        self._mount()
        r = self._run("hls", "-1aF", b":" + folder)
        return [line[:-1] for line in r.stdout.split(b"\n") if line.endswith(b":")]

    def macbinary(self, path):
        """One file (raw Mac path, bytes) as MacBinary, or None."""
        key = hashlib.sha1(str(self.image).encode() + path).hexdigest()[:16]
        cached = self.cache / f"{key}.bin"
        if not cached.exists():
            self._mount()
            r = self._run("hcopy", "-m", b":" + path, str(cached))
            if r.returncode or not cached.exists():
                return None
        return cached.read_bytes()

    def resources(self, path):
        """Resource map of one file (raw Mac path, bytes)."""
        blob = self.macbinary(path)
        if blob is None:
            return {}
        try:
            return parse_resource_fork(macbinary_forks(blob)[1])
        except (struct.error, IndexError, UnicodeDecodeError):
            return {}

    def system_files(self):
        """[(display name, {(type,id): (name, data)})] for the boot System Folder."""
        out = []
        for folder in SYSTEM_DIRS:
            for name in self.files(folder):
                res = self.resources(folder + b":" + name)
                if res:
                    out.append((name.decode("mac_roman"), res))
        return out

    def close(self):
        if self._mounted:
            self._run("humount")
            self._mounted = False
