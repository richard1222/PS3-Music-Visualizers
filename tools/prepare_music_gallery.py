"""Offline four-artwork native music UI experiment; timed fades, no audio input."""
import argparse,copy
from pathlib import Path
import xml.etree.ElementTree as ET
from prepare_music_plane import add_probe,IMAGE_NAME,PLANE_NAME

GALLERY_NAME='saber_music_gallery_fades'

def check_image_budget(sizes):
    if len(sizes)!=4 or any(type(n) is not int or n<16 for n in sizes):
        raise ValueError('Four valid GIM sizes required')
    if sum(sizes)>600000:
        raise ValueError('Gallery artwork exceeds the tested 600000-byte budget; larger native tests showed blank pictures')

def add_gallery(xml,images,cycles=8,loop=False):
    if len(images)!=4:raise ValueError('Exactly four reviewed images covering the selected seven characters required')
    if type(cycles) is not int or not 1<=cycles<=8:raise ValueError('Cycles must be between one and eight')
    if type(loop) is not bool or (loop and cycles!=1):raise ValueError('Loop mode requires exactly one forty-second cycle')
    root=ET.fromstring(add_probe(xml,images[0],target='audioplayer'))
    main=root.find('MainTree');image_tree=main.find('ImageTree');page=main.find("ObjectTree/Page[@name='full_screen_page']")
    if main.find('AnimTree') is not None:raise ValueError('Existing animations preserved')
    first=page[0];first.set('alphaScale','0.6');first.set('onLoad','nothing')
    labels=[PLANE_NAME]
    for i,image in enumerate(images[1:],1):
        label=PLANE_NAME+'_'+str(i);image_label=IMAGE_NAME+'_'+str(i)
        if any(n.get('name') in (label,image_label,GALLERY_NAME) for n in root.iter()):raise ValueError('Existing gallery label preserved')
        ET.SubElement(image_tree,'Image',name=image_label,src=str(image),format='gim',compression='zlib',unknownByte='0')
        plane=copy.deepcopy(first);plane.set('name',label);plane.set('image','image:'+image_label)
        plane.set('alphaScale','0.6');plane.set('onLoad','nothing');page.append(plane);labels.append(label)
    # Start only after the final artwork plane loads. Keep equal base alpha;
    # the animation's separate fade factor selects which picture is visible.
    page[-1].set('onLoad','anim:'+GALLERY_NAME)
    animation=ET.SubElement(ET.SubElement(main,'AnimTree'),'Animation',name=GALLERY_NAME)
    def fade(index,duration,alpha):
        ET.SubElement(animation,'Fade',object='object:'+labels[index],duration=str(duration),accelMode='0x3',transparency=str(alpha))
    for i in range(4):fade(i,0,1 if i==0 else 0)
    # Finite 320-second maximum: never recurse into another event or modify
    # native controls. The final image starts the sequence exactly once.
    for _ in range(cycles):
        for current in range(4):
            ET.SubElement(animation,'Delay',time='8000')
            fade(current,2000,0);fade((current+1)%4,2000,1)
            ET.SubElement(animation,'Delay',time='2000')
    if loop:
        # Native RCO animation events can restart an animation. The single
        # self event comes after 40000 ms of delays; never a zero-delay loop.
        ET.SubElement(animation,'FireEvent',object='anim:'+GALLERY_NAME)
    return root

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source_xml',type=Path);p.add_argument('output_xml',type=Path)
    p.add_argument('images',nargs=4,type=Path,help='Reviewed Miku, Rin/Saber, Nami/Robin, Rias/Akeno GIMs, in that order')
    p.add_argument('--cycles',type=int,default=8)
    p.add_argument('--loop',action='store_true',help='Experimental native self-event after a full 40-second cycle; requires --cycles 1')
    a=p.parse_args()
    if a.output_xml.exists():p.error('Existing output preserved')
    check_image_budget([image.stat().st_size for image in a.images])
    for image in a.images:
        with image.open('rb') as f:header=f.read(16)
        if not header.startswith(b'.GIM1.00'):p.error('Expected GIM images')
    root=add_gallery(a.source_xml.read_bytes(),[str(i.resolve()) for i in a.images],a.cycles,loop=a.loop)
    for node in root.iter():
        value=node.get('src')
        if value and not Path(value).is_absolute():node.set('src',str((a.source_xml.parent/value).resolve()))
    ET.indent(root,space='\t')
    with a.output_xml.open('xb') as out:out.write(ET.tostring(root,encoding='iso-8859-1',xml_declaration=True))
    print('OFFLINE: timed gallery prepared; console behavior and audio response require separate verification')
