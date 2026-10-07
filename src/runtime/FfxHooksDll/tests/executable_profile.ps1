# Jarvis-HOOK: private native test fixtures follow the explicitly selected compiler profile.
# This returns an independently recorded artifact hash; it cannot accept arbitrary input hashes.
function Get-FfxTestExecutableHash {
    if ($env:CL -match '(?i)(?:^|\s)/DFFXHOOKS_TARGET_STEAM_20261001(?:=\S+)?(?:\s|$)') {
        return '0537B2A1047F3266E73495CD4E35F63F0777F4231D417699F979954686DA686D'
    }
    return '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'
}
