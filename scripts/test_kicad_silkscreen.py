"""Silkscreen geometry regressions; run with KiCad's bundled Python."""

import contextlib
import io
import runpy
import tempfile
import unittest
from pathlib import Path


api = runpy.run_path(str(Path(__file__).with_name("build-kicad-pcb.py")))
pcbnew = api["pcbnew"]
mm = api["mm"]
vec = api["vec"]


class SilkscreenTests(unittest.TestCase):
    def setUp(self):
        self.board = pcbnew.BOARD()
        self.via = pcbnew.PCB_VIA(self.board)
        self.via.SetPosition(vec(5, 5))
        self.via.SetWidth(mm(0.8))
        self.via.SetDrill(mm(0.4))
        self.board.Add(self.via)

    def check(self):
        with contextlib.redirect_stdout(io.StringIO()):
            api["check_silkscreen"](self.board)

    def add_line(self, start=(1, 5), end=(9, 5), parent=None):
        parent = self.board if parent is None else parent
        line = pcbnew.PCB_SHAPE(parent)
        line.SetShape(pcbnew.SHAPE_T_SEGMENT)
        line.SetLayer(pcbnew.F_SilkS)
        line.SetWidth(mm(0.16))
        line.SetStart(vec(*start))
        line.SetEnd(vec(*end))
        parent.Add(line)
        return line

    def test_detects_hole_collision_on_both_sides(self):
        for layer in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            with self.subTest(layer=layer):
                line = self.add_line()
                line.SetLayer(layer)
                with self.assertRaisesRegex(SystemExit, "from via"):
                    self.check()
                self.board.Remove(line)

    def test_rejects_thin_strokes_and_accepts_printable_strokes(self):
        line = self.add_line((1, 1), (9, 1))
        line.SetWidth(mm(0.15))
        with self.assertRaisesRegex(SystemExit, "stroke 0.150"):
            self.check()
        line.SetWidth(mm(0.16))
        self.check()

    def test_gaps_outline_without_changing_copper_and_is_idempotent(self):
        self.add_line()
        copper = api["track_signatures"](self.board)
        api["gap_silkscreen_lines"](self.board)
        self.check()
        self.assertEqual(len(api["silkscreen_items"](self.board)), 2)
        self.assertEqual(api["track_signatures"](self.board), copper)
        api["gap_silkscreen_lines"](self.board)
        self.assertEqual(len(api["silkscreen_items"](self.board)), 2)
        self.check()

    def test_gaps_footprint_graphics(self):
        footprint = pcbnew.FOOTPRINT(self.board)
        footprint.Reference().SetVisible(False)
        footprint.SetPosition(vec(12, 9))
        footprint.SetOrientationDegrees(90)
        self.board.Add(footprint)
        self.add_line(parent=footprint)
        api["gap_silkscreen_lines"](self.board)
        self.assertEqual(len(list(footprint.GraphicalItems())), 2)
        self.check()

    def test_resolves_inherited_drills_and_survives_reload(self):
        self.via.SetDrillDefault()
        drill = self.via.GetDrillValue()
        self.assertGreater(drill, 0)
        self.assertEqual(api["silkscreen_holes"](self.board)[0][1].GetWidth(),
                         drill)
        self.add_line()
        api["gap_silkscreen_lines"](self.board)
        self.check()
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "check.kicad_pcb")
            self.assertTrue(pcbnew.SaveBoard(path, self.board))
            self.board = pcbnew.LoadBoard(path)
            self.check()

    def test_enforces_clearance_threshold(self):
        line = self.add_line((1, 5.48), (9, 5.48))
        self.check()
        line.SetStart(vec(1, 5.479))
        line.SetEnd(vec(9, 5.479))
        with self.assertRaisesRegex(SystemExit, "from via"):
            self.check()

    def test_detects_visible_text_and_ignores_hidden_fields(self):
        footprint = pcbnew.FOOTPRINT(self.board)
        footprint.SetReference("U1")
        self.board.Add(footprint)
        reference = footprint.Reference()
        reference.SetPosition(vec(5, 5))
        reference.SetLayer(pcbnew.F_SilkS)
        reference.SetTextSize(vec(1, 1))
        reference.SetTextThickness(mm(0.16))
        reference.SetVisible(True)
        with self.assertRaisesRegex(SystemExit, "text 'U1'"):
            self.check()
        reference.SetVisible(False)
        self.check()

    def add_pad(self, position=(5, 12), size=(2, 1), layers=None,
                orientation=0):
        footprint = pcbnew.FOOTPRINT(self.board)
        footprint.SetReference("P1")
        footprint.Reference().SetVisible(False)
        self.board.Add(footprint)
        pad = pcbnew.PAD(footprint)
        pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetShape(pcbnew.PAD_SHAPE_RECT)
        pad.SetLayerSet(layers or pad.SMDMask())
        pad.SetSize(vec(*size))
        pad.SetPosition(vec(*position))
        pad.SetOrientationDegrees(orientation)
        pad.SetNumber("1")
        footprint.Add(pad)
        return pad

    def test_enforces_same_side_pad_clearance_threshold(self):
        self.add_pad()
        # Pad edge y=12.5; 0.2 mm clearance plus half of 0.16 mm stroke.
        line = self.add_line((1, 12.78), (9, 12.78))
        self.check()
        line.SetStart(vec(1, 12.779))
        line.SetEnd(vec(9, 12.779))
        with self.assertRaisesRegex(SystemExit, "from pad P1.1"):
            self.check()
        line.SetLayer(pcbnew.B_SilkS)
        self.check()

    def test_includes_mask_expansion_in_pad_clearance(self):
        pad = self.add_pad()
        pad.SetLocalSolderMaskMargin(mm(0.1))
        self.add_line((1, 12.80), (9, 12.80))
        with self.assertRaisesRegex(SystemExit, "from pad P1.1"):
            self.check()

    def test_gaps_lines_around_rotated_rectangular_pads(self):
        self.add_pad(orientation=45)
        self.add_line((1, 12), (9, 12))
        self.add_line((5, 8), (5, 16))
        copper = api["track_signatures"](self.board)
        with self.assertRaisesRegex(SystemExit, "from pad P1.1"):
            self.check()
        api["gap_silkscreen_lines"](self.board)
        self.check()
        self.assertEqual(len(api["silkscreen_items"](self.board)), 4)
        self.assertEqual(api["track_signatures"](self.board), copper)

    def test_checks_nonplated_drills(self):
        footprint = pcbnew.FOOTPRINT(self.board)
        footprint.SetReference("H1")
        footprint.Reference().SetVisible(False)
        self.board.Add(footprint)
        pad = pcbnew.PAD(footprint)
        pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        pad.SetPosition(vec(5, 2))
        pad.SetDrillSize(vec(2.7, 2.7))
        footprint.Add(pad)
        self.add_line((1, 2), (9, 2))
        with self.assertRaisesRegex(SystemExit, "from H1"):
            self.check()
        api["gap_silkscreen_lines"](self.board)
        self.check()


if __name__ == "__main__":
    unittest.main()
