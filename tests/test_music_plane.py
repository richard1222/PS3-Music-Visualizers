import copy,sys,unittest,xml.etree.ElementTree as ET,subprocess,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_music_plane import add_probe,IMAGE_NAME,PLANE_NAME,MOTION_NAME


def fixture():
    return '''<RcoFile><MainTree name="soundvisualizer"><ImageTree><Image name="original" src="original.gim"/></ImageTree><ObjectTree><Page name="page_soundvisualizer0" onInit="event:native:/OnPageInit"/><Page name="page_soundvisualizer1" onInit="event:native:/OnPageInit"/><Page name="page_soundvisualizer2"><Plane name="page2_plane_seat" image="nothing" alphaScale="0.4"><Button name="control"/></Plane></Page></ObjectTree></MainTree></RcoFile>'''


def audio_fixture():
    root=ET.fromstring('<RcoFile><MainTree name="sample"><ImageTree><Image name="original" src="original.gim"/></ImageTree><ObjectTree/></MainTree></RcoFile>')
    objects=root.find('MainTree/ObjectTree')
    for name,callback in [('full_screen_page','OnFullScreenPageInit'),('back_screen_page','OnBackScreenPageInit'),
                          ('control_panel_page','OnControlPanelPageInit'),('title_information_page','OnTitleInformationPageInit')]:
        page=ET.SubElement(objects,'Page',name=name,onInit='event:native:/AudioPlayerPlugin::'+callback)
        if name=='back_screen_page':ET.SubElement(page,'Plane',name='back_screen_plane',image='image:original',alphaScale='0')
        if name=='control_panel_page':ET.SubElement(page,'Button',name='keep_control')
    return ET.tostring(root,encoding='unicode')


class MusicPlaneTests(unittest.TestCase):
    def test_existing_resource_survives_probe_removal(self):
        source=fixture();out=ET.fromstring(add_probe(source,'art.gim'))
        main=out.find('MainTree');images=main.find('ImageTree');objects=main.find('ObjectTree')
        self.assertEqual(len(images),2)
        self.assertEqual(images[-1].get('name'),IMAGE_NAME)
        self.assertEqual(objects[0][0].get('name'),PLANE_NAME)
        self.assertEqual(len(objects[0][0]),0) # No copied controls or event children.
        self.assertEqual(objects[0].get('onInit'),'event:native:/OnPageInit')
        images.remove(images[-1]);objects[0].remove(objects[0][0])
        self.assertEqual(ET.tostring(out),ET.tostring(ET.fromstring(source)))
    def test_existing_probe_refused(self):
        with self.assertRaisesRegex(ValueError,'Existing probe'):add_probe(add_probe(fixture(),'art.gim'),'new.gim')
    def test_nonempty_target_preserved(self):
        source=fixture().replace('onInit="event:native:/OnPageInit"/>','onInit="event:native:/OnPageInit"><Text name="keep"/></Page>',1)
        with self.assertRaisesRegex(ValueError,'empty music page'):add_probe(source,'art.gim')
    def test_wrong_resource_refused(self):
        with self.assertRaisesRegex(ValueError,'soundvisualizer'):add_probe(fixture().replace('name="soundvisualizer"','name="custom_render_plugin"'),'art.gim')
    def test_missing_native_template_refused(self):
        with self.assertRaisesRegex(ValueError,'template'):add_probe(fixture().replace('page2_plane_seat','different'),'art.gim')
    def test_audioplayer_requires_explicit_profile_and_preserves_existing_ui(self):
        source=audio_fixture()
        with self.assertRaises(ValueError):add_probe(source,'art.gim')
        out=ET.fromstring(add_probe(source,'art.gim',target='audioplayer'))
        main=out.find('MainTree');images=main.find('ImageTree');objects=main.find('ObjectTree')
        self.assertEqual(objects[0][0].get('name'),PLANE_NAME)
        images.remove(images[-1]);objects[0].remove(objects[0][0])
        self.assertEqual(ET.tostring(out),ET.tostring(ET.fromstring(source)))
    def test_generic_sample_resource_and_changed_callbacks_refused(self):
        source=audio_fixture().replace('OnControlPanelPageInit','DifferentCallback')
        with self.assertRaisesRegex(ValueError,'native page'):add_probe(source,'art.gim',target='audioplayer')
    def test_audioplayer_populated_fullscreen_page_preserved(self):
        root=ET.fromstring(audio_fixture())
        ET.SubElement(root.find('MainTree/ObjectTree/Page'),'Text',name='keep')
        with self.assertRaisesRegex(ValueError,'empty music page'):add_probe(ET.tostring(root),'art.gim',target='audioplayer')
    def test_unknown_profile_refused(self):
        with self.assertRaisesRegex(ValueError,'Unknown'):add_probe(audio_fixture(),'art.gim',target='other')
    def test_new_artwork_does_not_inherit_stock_layout_size_overrides(self):
        source=audio_fixture().replace('name="back_screen_plane"','name="back_screen_plane" unknownInt20="0x64f" unknownInt21="0x650"')
        out=ET.fromstring(add_probe(source,'art.gim',target='audioplayer'))
        new=next(n for n in out.iter('Plane') if n.get('name')==PLANE_NAME)
        old=next(n for n in out.iter('Plane') if n.get('name')=='back_screen_plane')
        self.assertEqual((new.get('width'),new.get('height')),('1824','1026'))
        self.assertEqual((new.get('unknownInt20'),new.get('unknownInt21')),('0x0','0x0'))
        self.assertEqual((old.get('unknownInt20'),old.get('unknownInt21')),('0x64f','0x650'))
    def test_cli_formats_appended_image_for_compiler(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);(folder/'input.xml').write_text(fixture())
            (folder/'art.gim').write_bytes(b'.GIM1.00'+bytes(8))
            tool=Path(__file__).resolve().parents[1]/'tools'/'prepare_music_plane.py'
            result=subprocess.run([sys.executable,str(tool),str(folder/'input.xml'),str(folder/'art.gim'),str(folder/'output.xml')],capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            data=(folder/'output.xml').read_bytes()
            self.assertTrue(data.startswith(b'<?xml'))
            self.assertIn(b'\n\t\t\t<Image name="'+IMAGE_NAME.encode(),data)
    def test_motion_probe_is_finite_and_targets_only_new_plane(self):
        out=ET.fromstring(add_probe(fixture(),'art.gim',motion=True))
        animation=out.find('MainTree/AnimTree/Animation')
        self.assertEqual(animation.get('name'),MOTION_NAME)
        self.assertEqual([n.get('time') for n in animation if n.tag=='Delay'],['12000','12000'])
        self.assertFalse(list(animation.iter('FireEvent')))
        for node in animation:
            if node.get('object'):self.assertEqual(node.get('object'),'object:'+PLANE_NAME)
        planes=[n for n in out.iter('Plane') if n.get('name')==PLANE_NAME]
        self.assertEqual(planes[0].get('onLoad'),'anim:'+MOTION_NAME)


if __name__=='__main__':unittest.main()
