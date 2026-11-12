import unittest,xml.etree.ElementTree as ET
from test_music_plane import audio_fixture
from prepare_music_gallery import add_gallery,GALLERY_NAME,check_image_budget
from prepare_music_plane import IMAGE_NAME,PLANE_NAME

class MusicGalleryTests(unittest.TestCase):
    def test_original_assets_controls_and_page_events_survive(self):
        source=audio_fixture();out=add_gallery(source,['a.gim','b.gim','c.gim','d.gim'])
        main=out.find('MainTree');images=main.find('ImageTree');page=main.find("ObjectTree/Page[@name='full_screen_page']")
        self.assertEqual(len(page),4)
        self.assertEqual([n.get('alphaScale') for n in page],['0.6']*4)
        self.assertEqual(sum(n.get('onLoad')!='nothing' for n in page),1)
        self.assertEqual(page[-1].get('onLoad'),'anim:'+GALLERY_NAME)
        for n in list(images)[1:]:images.remove(n)
        for n in list(page):page.remove(n)
        main.remove(main.find('AnimTree'))
        self.assertEqual(ET.tostring(out),ET.tostring(ET.fromstring(source)))
    def test_finite_sequence_only_targets_new_artwork(self):
        out=add_gallery(audio_fixture(),['a','b','c','d'],cycles=1)
        animation=out.find('MainTree/AnimTree/Animation')
        self.assertEqual(animation.get('name'),GALLERY_NAME)
        self.assertFalse(list(animation.iter('FireEvent')))
        self.assertEqual(sum(int(n.get('time')) for n in animation if n.tag=='Delay'),40000)
        labels={n.get('name') for n in out.iter('Plane') if n.get('name','').startswith(PLANE_NAME)}
        for n in animation:
            if n.tag=='Fade':self.assertIn(n.get('object')[7:],labels)
    def test_extra_artwork_and_unbounded_cycles_refused(self):
        with self.assertRaises(ValueError):add_gallery(audio_fixture(),['a']*5)
        for cycles in [0,9,True,1.5]:
            with self.assertRaises(ValueError):add_gallery(audio_fixture(),['a']*4,cycles)
    def test_image_budget_accepts_hardware_case_and_refuses_blank_picture_case(self):
        check_image_budget([147584]*4)
        with self.assertRaisesRegex(ValueError,'budget'):check_image_budget([589952]*4)
        with self.assertRaises(ValueError):check_image_budget([147584]*3)

if __name__=='__main__':unittest.main()
