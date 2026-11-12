"""Install only new HDD files. No flash, registry, theme or trophy writes."""
from pathlib import Path
import argparse,ftplib,hashlib,io,ipaddress,json,urllib.request
from qrc import inspect_qrc

WEB_ROOT = "/dev_hdd0/PS3-Music-Visualizers"
EARTH_ROOT = "/dev_hdd0/tmp/earth"

def digest(data): return hashlib.sha256(data).hexdigest()

def build_plan(planets=None, web=None):
    files=[]
    if planets:
        folder=Path(planets);manifest=json.loads((folder/"manifest.json").read_text())
        slots=manifest["slots"]
        if not 1 <= len(slots) <= 254: raise ValueError("Slot count")
        seen=set()
        for item in slots:
            slot=item["slot"]
            if type(slot) is not int or not 1 <= slot <= 254 or slot in seen:
                raise ValueError("Invalid or duplicate slot")
            seen.add(slot);leaf=str(slot)+".qrc"
            if item["file"] != leaf: raise ValueError("Unsafe resource filename")
            data=(folder/leaf).read_bytes();detail=inspect_qrc(data)
            if detail["sha256"] != item["sha256"]: raise ValueError("Resource hash differs")
            files.append({"source":folder/leaf,"target":EARTH_ROOT+"/"+leaf,"data":data})
    if web:
        folder=Path(web)
        for leaf in ("index.html","visualizer.js","art/miku-city.jpg","art/miku-cosmic.jpg","art/miku-piano.jpg","art/credits.json"):
            p=folder/leaf;data=p.read_bytes()
            if len(data)>8*1024*1024: raise ValueError("Web asset bound")
            if p.suffix==".jpg" and not data.startswith(b"\xff\xd8"):raise ValueError("JPEG expected")
            files.append({"source":p,"target":WEB_ROOT+"/"+leaf,"data":data})
    if not files:raise ValueError("Select --planets or --web")
    return files

def private_host(host):
    address=ipaddress.ip_address(host)
    if address.version!=4 or not address.is_private or address.is_loopback:
        raise ValueError("Use the PS3's private IPv4 LAN address")
    return str(address)

def read_remote(ftp,path,limit):
    data=bytearray()
    def part(chunk):
        if len(data)+len(chunk)>limit:raise ValueError("Remote read bound")
        data.extend(chunk)
    ftp.retrbinary("RETR "+path,part)
    return bytes(data)

def upload_new(ftp,item,replace_sha=None):
    path,data=item["target"],item["data"]
    original=None
    try:
        existing=read_remote(ftp,path,max(len(data),16*1024*1024))
    except ftplib.error_perm as error:
        if not str(error).startswith("550"):raise
    else:
        if existing==data:return "already-matches"
        if replace_sha is None or digest(existing)!=replace_sha:
            raise ValueError("Existing different file preserved: "+path)
        original=existing
    temporary=path+".stage-"+digest(data)[:12]
    try:ftp.size(temporary)
    except ftplib.error_perm as error:
        if not str(error).startswith("550"):raise
    else:raise ValueError("Existing staged file preserved: "+temporary)
    ftp.storbinary("STOR "+temporary,io.BytesIO(data))
    if read_remote(ftp,temporary,len(data))!=data:raise ValueError("Staging readback differs")
    # Recheck before rename. Installation is intended for one operator at a time.
    if original is None:
        try:ftp.size(path)
        except ftplib.error_perm as error:
            if not str(error).startswith("550"):raise
        else:raise ValueError("Destination appeared during upload: "+path)
        ftp.rename(temporary,path)
    else:
        if read_remote(ftp,path,len(original))!=original:raise ValueError("Owned file changed during staging")
        preserved=path+".backup-"+digest(original)[:12]
        try:ftp.size(preserved)
        except ftplib.error_perm as error:
            if not str(error).startswith("550"):raise
        else:raise ValueError("Existing backup preserved: "+preserved)
        ftp.rename(path,preserved)
        try:ftp.rename(temporary,path)
        except Exception:
            ftp.rename(preserved,path)
            raise
        if read_remote(ftp,preserved,len(original))!=original:raise ValueError("Backup readback differs")
    if read_remote(ftp,path,len(data))!=data:raise ValueError("Final readback differs")
    return "updated-with-backup" if original is not None else "installed"

def install(host,files,activate=None,previous=None):
    host=private_host(host)
    if activate is not None and (type(activate) is not int or not 1<=activate<=254):
        raise ValueError("Activation slot bound")
    if activate is not None and EARTH_ROOT+"/"+str(activate)+".qrc" not in [i["target"] for i in files]:
        raise ValueError("Activation slot is not in this verified plan")
    with urllib.request.urlopen("http://"+host+"/cpursx.ps3",timeout=6) as response:status=response.read(65537)
    if len(status)>65536 or b"(XMB)" not in status:raise ValueError("XMB required")
    receipt={"complete":False,"flash_writes":False,"files":[]}
    with ftplib.FTP(host,timeout=10) as ftp:
        ftp.login();ftp.voidcmd("TYPE I")
        directories=sorted({str(Path(i["target"]).parent).replace("\\","/") for i in files},key=lambda x:(x.count("/"),x))
        for folder in directories:
            # All directories stay under the two fixed HDD roots.
            if not (folder==EARTH_ROOT or folder==WEB_ROOT or folder==WEB_ROOT+"/art"):
                raise ValueError("Unexpected destination directory")
            try:ftp.mkd(folder)
            except ftplib.error_perm:
                ftp.cwd(folder);ftp.cwd("/")
        for item in files:
            state=upload_new(ftp,item,(previous or {}).get(item["target"]))
            receipt["files"].append({"path":item["target"],"bytes":len(item["data"]),"sha256":digest(item["data"]),"state":state})
        for item in files:
            if read_remote(ftp,item["target"],len(item["data"]))!=item["data"]:
                raise ValueError("Final verification differs")
    if activate is not None:
        receipt["activation_requested"]=activate
        try:
            with urllib.request.urlopen("http://"+host+"/earth.ps3?"+str(activate),timeout=8) as response:response.read(65537)
        except Exception as error:
            receipt["activation_http_error"]=type(error).__name__+": "+str(error)
        wanted=next(i["data"] for i in files if i["target"]==EARTH_ROOT+"/"+str(activate)+".qrc")
        with ftplib.FTP(host,timeout=10) as ftp:
            ftp.login()
            mapped=read_remote(ftp,"/dev_flash/vsh/resource/qgl/earth.qrc",16*1024*1024)
        receipt["activation_mapping_verified"]=mapped==wanted
        if not receipt["activation_mapping_verified"]:
            receipt["complete"]=False
            return receipt
    receipt["complete"]=True
    return receipt

if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--planets",type=Path);p.add_argument("--web",type=Path)
    p.add_argument("--host");p.add_argument("--apply",action="store_true");p.add_argument("--activate",type=int);p.add_argument("--receipt",type=Path);p.add_argument("--previous-receipt",type=Path)
    a=p.parse_args();plan=build_plan(a.planets,a.web)
    if a.apply:
        if not a.host or not a.receipt:p.error("--apply requires --host and --receipt")
        if a.receipt.exists():p.error("Receipt exists")
        previous=None
        if a.previous_receipt:
            previous={i["path"]:i["sha256"] for i in json.loads(a.previous_receipt.read_text())["files"]}
        result=install(a.host,plan,a.activate,previous)
        a.receipt.parent.mkdir(parents=True,exist_ok=True)
        with a.receipt.open("x",encoding="utf-8") as f:json.dump(result,f,indent=2)
    else:
        result={"dry_run":True,"network_used":False,"files":[{"path":i["target"],"bytes":len(i["data"]),"sha256":digest(i["data"])} for i in plan]}
    print(json.dumps(result,indent=2))
