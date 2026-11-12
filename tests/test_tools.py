from pathlib import Path
import ftplib,hashlib,io,json,struct,sys,tempfile,unittest,zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from qrc import inspect_qrc
from install_hdd import build_plan,private_host,upload_new

def resource():
    raw=struct.pack(">4s13I",b"QRCF",0x110,*([64,0]*6))+bytes(8)
    return b"QRCC"+struct.pack(">I",len(raw))+zlib.compress(raw)

class FTP:
    def __init__(self,files=None,corrupt=False):self.files=dict(files or {});self.corrupt=corrupt;self.writes=[]
    def retrbinary(self,command,callback):
        path=command[5:]
        if path not in self.files:raise ftplib.error_perm("550 absent")
        callback(self.files[path])
    def size(self,path):
        if path not in self.files:raise ftplib.error_perm("550 absent")
        return len(self.files[path])
    def storbinary(self,command,stream):
        path=command[5:];self.writes.append(path);self.files[path]=stream.read()+(b"bad" if self.corrupt else b"")
    def rename(self,source,target):self.files[target]=self.files.pop(source)

class QrcTests(unittest.TestCase):
    def test_valid_bounded_container(self):self.assertEqual(inspect_qrc(resource())["uncompressed_bytes"],64)
    def test_wrong_magic(self):
        with self.assertRaises(ValueError):inspect_qrc(b"NOPE"+resource()[4:])
    def test_false_declared_size(self):
        data=resource()
        with self.assertRaises(ValueError):inspect_qrc(data[:4]+struct.pack(">I",65)+data[8:])
    def test_truncated_stream(self):
        with self.assertRaises(ValueError):inspect_qrc(resource()[:-2])
    def test_trailing_bytes(self):
        with self.assertRaises(ValueError):inspect_qrc(resource()+b"junk")
    def test_outside_table(self):
        raw=bytearray(zlib.decompress(resource()[8:]));struct.pack_into(">I",raw,8,65)
        with self.assertRaises(ValueError):inspect_qrc(b"QRCC"+struct.pack(">I",64)+zlib.compress(raw))

class InstallTests(unittest.TestCase):
    def item(self):return {"target":"/dev_hdd0/tmp/earth/1.qrc","data":resource()}
    def test_new_upload_readback(self):
        ftp=FTP();item=self.item();self.assertEqual(upload_new(ftp,item),"installed");self.assertEqual(ftp.files[item["target"]],item["data"])
    def test_existing_matching_preserved(self):
        item=self.item();ftp=FTP({item["target"]:item["data"]});self.assertEqual(upload_new(ftp,item),"already-matches");self.assertEqual(ftp.writes,[])
    def test_existing_different_preserved(self):
        item=self.item();ftp=FTP({item["target"]:b"original"})
        with self.assertRaises(ValueError):upload_new(ftp,item)
        self.assertEqual(ftp.files[item["target"]],b"original");self.assertEqual(ftp.writes,[])
    def test_corrupt_stage_never_installed(self):
        ftp=FTP(corrupt=True);item=self.item()
        with self.assertRaises(ValueError):upload_new(ftp,item)
        self.assertNotIn(item["target"],ftp.files)
    def test_owned_update_keeps_backup(self):
        item=self.item();old=b"owned previous version";ftp=FTP({item["target"]:old})
        previous=hashlib.sha256(old).hexdigest()
        self.assertEqual(upload_new(ftp,item,previous),"updated-with-backup")
        self.assertEqual(ftp.files[item["target"]],item["data"])
        self.assertEqual(ftp.files[item["target"]+".backup-"+previous[:12]],old)
    def test_failed_owned_rename_restores_original(self):
        class FailingFTP(FTP):
            def rename(self,source,target):
                if ".stage-" in source:raise OSError("injected rename failure")
                super().rename(source,target)
        item=self.item();old=b"owned previous version";ftp=FailingFTP({item["target"]:old})
        with self.assertRaises(OSError):upload_new(ftp,item,hashlib.sha256(old).hexdigest())
        self.assertEqual(ftp.files[item["target"]],old)
    def test_private_address_only(self):
        self.assertEqual(private_host("192.168.2.10"),"192.168.2.10")
        for value in ("8.8.8.8","127.0.0.1","example.com","http://192.168.2.10"):
            with self.assertRaises(ValueError):private_host(value)
    def test_manifest_path_escape_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);(p/"manifest.json").write_text(json.dumps({"slots":[{"slot":1,"file":"../outside.qrc"}]}))
            with self.assertRaises(ValueError):build_plan(p)
    def test_hash_mismatch_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);(p/"1.qrc").write_bytes(resource());(p/"manifest.json").write_text(json.dumps({"slots":[{"slot":1,"file":"1.qrc","sha256":"0"*64}]}))
            with self.assertRaises(ValueError):build_plan(p)

if __name__=="__main__":unittest.main()
