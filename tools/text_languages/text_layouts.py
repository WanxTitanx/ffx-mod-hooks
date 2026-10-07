"""Closed MOD-006 text-container catalogue, matching the native validator."""
from dataclasses import dataclass
import re
from asset_io import AssetError

MASTER = 'ffx_ps2/ffx/master/new_uspc/'
KERNEL = MASTER + 'battle/kernel/'
MENU = ('menu_txt.bin', 'mmain_txt.bin', 'config_txt.bin', 'save_txt.bin')
BATTLE = ('arms_txt.bin', 'btl_txt.bin', 'btlend_txt.bin', 'build_txt.bin',
          'item_txt.bin', 'name_txt.bin', 'status_txt.bin', 'summon_txt.bin')
EXTENDED = {'a_ability.bin':108, 'command.bin':96, 'important.bin':20, 'item.bin':96,
            'monmagic1.bin':92, 'monmagic2.bin':92, 'monster1.bin':128,
            'monster2.bin':128, 'monster3.bin':128, 'panel.bin':24,
            'sphere.bin':16, 'w_name.bin':72}
# The sphere suffix is gameplay data. These four UI suffixes are inherited
# native bytes, unchanged across Western locales, not localized help strings.
HELP_ONLY = {'btl_txt.bin', 'btlend_txt.bin', 'build_txt.bin', 'name_txt.bin',
             'save_txt.bin', 'sphere.bin'}
EVENT = re.compile(r'event/(obj_ps3|obj_psv)/[a-z0-9]{2}/(?P<id>[a-z][a-z0-9_-]{0,31})/(?P=id)\.bin\Z')
BANK = re.compile(r'battle/btl/(?P<id>[a-z][a-z0-9_-]{0,31})/(?P=id)\.bin\Z')


@dataclass(frozen=True)
class Layout:
    key: str
    member: str
    request: str
    family: str
    container: str
    minimum_api: int
    stride: int = 0
    slots: int = 0
    offset_step: int = 0


def describe_resource(name: str) -> Layout:
    if not isinstance(name, str):
        raise AssetError('Resource name must be a string')
    key = name.lower()
    leaf = key.removeprefix('battle/kernel/')
    if leaf in MENU + BATTLE or leaf in EXTENDED:
        stride = EXTENDED.get(leaf, 8 if leaf == 'btl_txt.bin' else 16)
        return Layout(leaf, KERNEL+leaf, '/ffx_data/'+KERNEL+leaf,
                      'menu' if leaf in MENU else 'battle', 'indexed',
                      3 if leaf in EXTENDED else 1, stride,
                      14 if leaf == 'w_name.bin' else (2 if leaf in HELP_ONLY else 4), 4)
    if EVENT.fullmatch(key):
        return Layout(key, MASTER+key, '/ffx_data/'+MASTER+key, 'events', 'field', 2, 8, 2, 4)
    if BANK.fullmatch(key) or key == 'menu/menumain.bin':
        return Layout(key, MASTER+key, '/ffx_data/'+MASTER+key,
                      'battle' if BANK.fullmatch(key) else 'menu', 'field', 3, 8, 2, 4)
    if key == 'menu/macrodic.dcp':
        return Layout(key, MASTER+key, '/ffx_data/'+MASTER+key, 'menu', 'macro', 3, 0, 2, 2)
    if 'lockit' in key:
        raise AssetError('Mixed-encoding lockit is extraction-only; no verified native consumer contract')
    raise AssetError(f'Unsupported text resource: {name}')
