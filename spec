#include "spec_common"

beginseg
	name "dmg_boot"
	flags RAW
	include "data/dmg_boot_placeholder.bin"
endseg

beginseg
	name "cgb_bios"
	flags RAW
	include "data/cgb_bios.bin"
endseg

beginseg
	name "gbrom"
	flags RAW
	include "data/OracleOfAges.gbc"
endseg

#include "spec_wave"
