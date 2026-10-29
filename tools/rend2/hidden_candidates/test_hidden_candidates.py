# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import unittest
from run import choose,request,semantic

class View:
    def __init__(self,rows):self.rows=rows
    def observations(self):return iter(self.rows)
def row(i,s,raw):
    return dict(id=f'{i:064x}',surface=s,position=[i%5,2,3],normal=[0,0,1],area=16,style=0,
                raw=raw,transfer=0,primaries=0)
class Protocol(unittest.TestCase):
    def test_selection_does_not_rank_global_brightness(self):
        rows=[row(i,i%3,[0,1,2])for i in range(1,30)]
        changed=copy.deepcopy(rows)
        for r in changed:r['raw']=[255,255,255]
        self.assertEqual([r['id']for r in choose(View(rows),None,512,12)],
                         [r['id']for r in choose(View(changed),None,512,12)])
    def test_stable_selection_independent_of_row_order(self):
        rows=[row(i,i%3,[4,5,6])for i in range(1,30)]
        self.assertEqual(choose(View(rows),None,512,12),choose(View(rows[::-1]),None,512,12))
    def test_request_has_no_answers_or_fixed_source_xyz(self):
        text=request([row(i,1,[1,2,3])for i in range(1,9)],'a'*64,[-100]*3+[100]*3,[-90]*3+[90]*3,False)
        self.assertTrue(text.startswith('JKHIDDEN1'))
        self.assertNotIn('answers',text)
        self.assertNotIn('amplitude',text)
        self.assertEqual(text.splitlines()[-2:],['2','0 1'])
    def test_unknown_colour_metadata_not_upgraded(self):
        text=request([row(1,1,[1,2,3])],'a'*64,[-100]*3+[100]*3,[-90]*3+[90]*3,False)
        self.assertEqual(text.splitlines()[6].split()[-1],'0')
    def test_semantic_comparison_keeps_unavailable_and_coverage(self):
        self.assertEqual(semantic(dict(code=1,resources={'seconds':2},candidates=[])),dict(code=1,candidates=[]))
        self.assertNotEqual(semantic(dict(code=0,candidates=[])),semantic(dict(code=1,candidates=[])))
if __name__=='__main__':unittest.main()
