import copy,sys,unittest,xml.etree.ElementTree as ET,subprocess,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_music_plane import add_probe,IMAGE_NAME,PLANE_NAME,MOTION_NAME


def fixture():
    return '''<RcoFile><MainTree name="soundvisualizer"><ImageTree><Image name="original" src="original.gim"/></ImageTree><ObjectTree><Page name="page_soundvisualizer0" onInit="event:native:/OnPageInit"/><Page name="page_soundvisualizer1" onInit="event:native:/OnPageInit"/><Page name="page_soundvisualizer2"><Plane name="page2_plane_seat" image="nothing" alphaScale="0.4"><Button name="control"/></Plane></Page></ObjectTree></MainTree></RcoFile>'''


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
