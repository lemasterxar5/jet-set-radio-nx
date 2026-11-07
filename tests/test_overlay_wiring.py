from pathlib import Path
import unittest


class OverlayWiring(unittest.TestCase):
    def test_present_only_after_successful_swap(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'source/jsr_gl.c').read_text()
        self.assertIn('if (b_eglSwapBuffers(g_dpy, g_surf)) {', source)
        self.assertLess(source.index('if (b_eglSwapBuffers(g_dpy, g_surf)) {'),
                        source.index('jsr_saltynx_present('))
        self.assertIn('g_real_viewport(left, top, width, height);', source)
        self.assertIn('jsr_saltynx_record_viewport(', source)
        self.assertNotIn('jsr_fps_', source)
        self.assertFalse((root / 'source/jsr_fps.c').exists())
        loader = (root / 'runtime/source/so_util.c').read_text()
        self.assertIn('port_import_interpose(name, (void *)address)', loader)

    def test_config_and_shutdown(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'source/jsr_main.c').read_text()
        self.assertIn('jsr_saltynx_exit();', source)
        startup = source[source.index('void port_run(void) {'):]
        self.assertLess(startup.index('jsr_saltynx_init();'), startup.index('so_execute_init_array('))
        self.assertNotIn('jsr_saltynx_init();', (root / 'source/jsr_gl.c').read_text())
        self.assertNotIn('"fps_overlay"', (root / 'source/jsr_config.c').read_text())


if __name__ == '__main__':
    unittest.main()
