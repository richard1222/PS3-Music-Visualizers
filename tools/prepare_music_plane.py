"""OFFLINE ONLY: add an image-plane probe to an owner's dumped music RCO.

No console connection, theme changes, shader changes or playback claims.
The page's runtime visibility and stacking order are unverified.
"""
from pathlib import Path
import argparse,copy,xml.etree.ElementTree as ET

IMAGE_NAME='saber_music_art_probe'
PLANE_NAME='saber_music_plane_probe'
MOTION_NAME='saber_music_motion_probe'


def add_probe(xml,image_path,motion=False,target='soundvisualizer'):
    root=ET.fromstring(xml)
    main=root.find('MainTree')
    if target not in ('soundvisualizer','audioplayer'):raise ValueError('Unknown music resource target')
    expected_name='soundvisualizer' if target=='soundvisualizer' else 'sample'
    if main is None or main.get('name')!=expected_name:raise ValueError('Expected '+target+' resource')
    images=main.find('ImageTree');objects=main.find('ObjectTree')
    if images is None or objects is None:raise ValueError('Image and object trees required')
    if any(node.get('name') in (IMAGE_NAME,PLANE_NAME,MOTION_NAME) for node in root.iter()):
        raise ValueError('Existing probe preserved')
    page_name='page_soundvisualizer0' if target=='soundvisualizer' else 'full_screen_page'
    template_name='page2_plane_seat' if target=='soundvisualizer' else 'back_screen_plane'
    pages=[node for node in objects if node.tag=='Page' and node.get('name')==page_name]
    if len(pages)!=1 or len(pages[0]):raise ValueError('One empty music page required: '+page_name)
    if target=='audioplayer':
        # "sample" alone identifies many unrelated Sony resources. Require
        # the specific native page callbacks before accepting this target.
        required={
            'full_screen_page':'OnFullScreenPageInit',
            'back_screen_page':'OnBackScreenPageInit',
            'control_panel_page':'OnControlPanelPageInit',
            'title_information_page':'OnTitleInformationPageInit',
        }
        for name,callback in required.items():
            matches=[n for n in objects if n.tag=='Page' and n.get('name')==name]
            if len(matches)!=1 or matches[0].get('onInit')!='event:native:/AudioPlayerPlugin::'+callback:
                raise ValueError('Expected audioplayer native page: '+name)
        templates=[node for page in objects if page.get('name')=='back_screen_page'
                   for node in page if node.tag=='Plane' and node.get('name')==template_name]
    else:
        templates=[node for node in objects.iter('Plane') if node.get('name')==template_name]
    if len(templates)!=1:raise ValueError('Expected one native plane template')
    plane=copy.deepcopy(templates[0])
    for child in list(plane):plane.remove(child)
    plane.attrib.update(name=PLANE_NAME,posX='0',posY='0',objectScale='1',
                        redScale='1',greenScale='1',blueScale='1',alphaScale='0.75',
                        width='960',height='540',image='image:'+IMAGE_NAME,onLoad='nothing')
    ET.SubElement(images,'Image',name=IMAGE_NAME,src=str(image_path),format='gim',compression='zlib',unknownByte='0')
    pages[0].append(plane)
    if motion:
        plane.set('onLoad','anim:'+MOTION_NAME)
        animations=main.find('AnimTree')
        if animations is None:animations=ET.SubElement(main,'AnimTree')
        animation=ET.SubElement(animations,'Animation',name=MOTION_NAME)
        def resize(duration,scale):
            ET.SubElement(animation,'Resize',object='object:'+PLANE_NAME,duration=str(duration),accelMode='0x3',width=str(scale),height=str(scale),unknownFloat4='1')
        def fade(duration,alpha):
            ET.SubElement(animation,'Fade',object='object:'+PLANE_NAME,duration=str(duration),accelMode='0x3',transparency=str(alpha))
        # A finite timed probe: no recursive event or endless animation loop.
        resize(0,0.97);fade(0,0.70)
        resize(12000,1);fade(12000,0.82)
        ET.SubElement(animation,'Delay',time='12000')
        resize(12000,0.97);fade(12000,0.70)
        ET.SubElement(animation,'Delay',time='12000')
    return ET.tostring(root,encoding='unicode')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source_xml',type=Path);p.add_argument('image_gim',type=Path);p.add_argument('output_xml',type=Path)
    p.add_argument('--motion',action='store_true',help='Add an unverified finite 24-second zoom/fade probe; no audio response')
    p.add_argument('--target',choices=['soundvisualizer','audioplayer'],default='soundvisualizer',
                   help='Explicit resource profile; audioplayer targets its empty full_screen_page (offline only)')
    a=p.parse_args()
    if a.output_xml.exists():p.error('Existing output preserved')
    with a.image_gim.open('rb') as stream:header=stream.read(16)
    if not header.startswith(b'.GIM1.00'):p.error('Expected locally prepared GIM input')
    result=add_probe(a.source_xml.read_bytes(),a.image_gim.resolve(),motion=a.motion,target=a.target)
    root=ET.fromstring(result)
    # Resolve original asset references relative to the source dump, never cwd.
    for node in root.iter():
        value=node.get('src')
        if value and not Path(value).is_absolute():node.set('src',str((a.source_xml.parent/value).resolve()))
    # Rcomage 1.1.1 omitted an appended image in compact XML. Match its
    # dumped XML layout, then independently dump and compare the built RCO.
    ET.indent(root,space='\t')
    with a.output_xml.open('xb') as stream:
        stream.write(ET.tostring(root,encoding='iso-8859-1',xml_declaration=True))
    print('OFFLINE ONLY: native UI probe prepared; visibility and compatibility unverified')
