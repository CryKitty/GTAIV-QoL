"""Steam config selection and one-step installer flow, with synthetic user data."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
def load():
 s=importlib.util.spec_from_file_location('steam_setup',ROOT/'tools/steam-deck/dependency_setup.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
class SteamWizardTests(unittest.TestCase):
 def tool(self,root,folder,name):
  path=root/'steamapps/common'/folder/'proton';path.parent.mkdir(parents=True);path.write_text('# fixture');path.chmod(0o755)
  (path.parent/'compatibilitytool.vdf').write_text('"compatibilitytools" { "compat_tools" { "'+name+'" { "install_path" "." } } }')
  return path.resolve()
 def config(self,root,mapping):
  (root/'config').mkdir(exist_ok=True);(root/'config/config.vdf').write_text('"InstallConfigStore" { "Software" { "Valve" { "Steam" { "CompatToolMapping" { '+mapping+' } } } } }')
 def test_app_mapping_overrides_default_and_never_opens_a_proton_choice(self):
  m=load()
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);wanted=self.tool(root,'Proton configured','proton_selected');self.tool(root,'Proton other','proton_default')
   self.config(root,'"0" { "name" "proton_default" } "12210" { "name" "proton_selected" }')
   prefix=root/'steamapps/compatdata/12210/pfx';(prefix/'drive_c/windows').mkdir(parents=True)
   with patch.object(m,'steam_roots',return_value=[root]):
    command,environment=m.proton_command(prefix,None)
   self.assertEqual(command,[str(wanted),'run']);self.assertEqual(environment['STEAM_COMPAT_DATA_PATH'],str(prefix.parent))
 def test_default_mapping_and_exact_last_run_tool(self):
  m=load()
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);wanted=self.tool(root,'Proton configured','proton_selected');self.tool(root,'Proton other','other')
   prefix=root/'steamapps/compatdata/12210/pfx';prefix.mkdir(parents=True)
   with patch.object(m,'steam_roots',return_value=[root]):
    self.config(root,'"0" { "name" "proton_selected" }');self.assertEqual(m.configured_proton(prefix),wanted)
    (root/'config/config.vdf').unlink();(prefix.parent/'config_info').write_text('fixture-version\n'+str(wanted.parent/'files/share/fonts')+'\n'+str(wanted.parent/'files/share/default_pfx'))
    self.assertEqual(m.configured_proton(prefix),wanted)
 def test_missing_configured_tool_never_uses_another_installed_version(self):
  m=load()
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.tool(root,'Proton other','other');self.config(root,'"12210" { "name" "missing_tool" }')
   prefix=root/'steamapps/compatdata/12210/pfx'
   with patch.object(m,'steam_roots',return_value=[root]):
    with self.assertRaisesRegex(m.SetupError,'configured in Steam is unavailable'):m.configured_proton(prefix)
 def test_ready_review_and_finished_screen_have_only_applicable_actions(self):
  m=load();screens=[];actions=iter(['next','install','done'])
  class UI:
   def choice(self,title,message,choices):screens.append((title,choices));return next(actions)
  with patch.object(m,'select_game',return_value=Path('/fixture/game')),patch.object(m,'existing_prefix',return_value=None),patch.object(m,'inspect_setup',return_value={'game':(True,'Game ready'),'loader':(True,'Loader ready')}),patch.object(m,'install_mod') as install:
   m.wizard(UI());install.assert_called_once()
  self.assertEqual(screens[1],('Ready to install',[('install','Install GTA IV QoL')]))
  self.assertEqual(screens[2],('GTA IV QoL installed',[('done','Finish')]))
 def test_terminal_deck_dialog_is_information_only(self):
  m=load();ui=m.DeckUI.__new__(m.DeckUI);ui.program='kdialog';ui.kde=True
  from types import SimpleNamespace
  with patch.object(m.subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout='')) as run:
   self.assertEqual(ui.choice('Done','Ready to play',[('done','Finish')]),'done')
   self.assertIn('--msgbox',run.call_args.args[0]);self.assertNotIn('--yesno',run.call_args.args[0])
 def test_library_prefix_is_preferred_over_an_unrelated_old_candidate(self):
  m=load()
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);game=root/'SD library/steamapps/common/GTA IV/GTAIV';game.mkdir(parents=True)
   correct=(root/'SD library/steamapps/compatdata/12210/pfx').resolve();other=root/'Old Steam/steamapps/compatdata/12210/pfx'
   with patch.object(m,'prefix_candidates',return_value=[other,correct]):self.assertEqual(m.existing_prefix(game),correct)
