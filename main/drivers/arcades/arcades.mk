# arcades.mk - the arcade games on the common frontends (common/arc.h) and
# their sources (relative to main/), one ARC_SRC_<id> line each: read by the
# Makefile, main/ports/web/Makefile, the CYD's and the RP2040 / RP2350's
# CMakeLists.txt and main/ports/android/build_arcade.sh. Every game also gets
# ARC_COMMON and its frontend (common/arc_sdl.c, arc_pico.c, arc_cyd.c).
# ARC_DEFS_<id>: its defines (without -D), when it has any (ARC_VARIANT: the
# game of a core with several). ARC_RAM_<id>: the objects the RP2040 / RP2350
# run from RAM (each line written out: CMake reads them as they are). Their ROMs come from storage (romfs: roms/<set>/ or MAME's
# <set>.zip), not built in.
# ARC_BIG: those that need more than the 4-8 MB of the VGA32 / RP2350 (desktop,
# web and Android only)
ARC_BIG := pirates simpsons
# the CYD builds of these keep their ROMs and graphics in the flash (the arccache
# partition, filled from the microSD on the first boot: partitions_arccache.csv)
ARC_FLASH := bankp citycon brkthru appoooh tehkanwc tigerh wboy pitfall2 teddybb mrviking seganinj upndown flicky gardia slapfght chinagat ddragon ddragon2 kungfum darwin combh robowres
ARCADES := bombjack calorie mrdo centiped milliped arkanoid minivadr commando gng blktiger bionicc atetris bagman ambush blueprnt btime shaolins mikie galaxian galaga pirates blockout bublbobl renegade bankp citycon brkthru appoooh tehkanwc tigerh wboy pitfall2 teddybb mrviking seganinj upndown flicky gardia slapfght wwfsstar shadoww chinagat ddragon ddragon2 armedf terraf cclimbr2 legion kungfum spelunkr spelunk2 ldrun ldrun2 rtype hharry rtype2 opwolf lnc mmonkey darwin saturnzi grasspin robowres combh mooncrst scramble frogger cabal bloodbro skysmash arabian digdug xevious sxevious bosco defender mayday colony7 joust robotron stargate elevator junglek tapper tron shollow domino wacko dotron timber depthch safari frogs sspaceat sspacaho headon headon2 headon2s invho2 nsub samurai invinco invds tranqgun spacetrk carnival brdrline digger pulsar heiankyo alphaho asteroid baraduke metrocrs punchout shuuz skullxbo ikari athena tnk3 aso actfancr sentetst cshift hattrick gghost otwalls snakepit triviag1 snakjack stocker triviabb triviag2 triviayp triviasp gimeabrk minigolf teamht grudge triviaes toggle nstocker sfootbal spiker stompin nametune rescraid aliens ajax simpsons
ARC_COMMON := drivers/arcades/common/arc_util.c rom/romfs.c machine/zx_inflate.c
ARC_SRC_bombjack := drivers/arcades/bombjack/bombjack.c cpu/z80dsp/z80dsp.c sound/ay8910.c machine/sega_decrypt.c
ARC_SRC_calorie := drivers/arcades/bombjack/bombjack.c cpu/z80dsp/z80dsp.c sound/ay8910.c machine/sega_decrypt.c
ARC_DEFS_calorie := ARC_VARIANT=1
ARC_SRC_mrdo := drivers/arcades/mrdo/mrdo.c cpu/z80dsp/z80dsp.c sound/sn76496.c
ARC_SRC_centiped := drivers/arcades/centipede/centipede.c cpu/m6502/m65xx.c sound/pokey.c
ARC_SRC_milliped := drivers/arcades/centipede/centipede.c cpu/m6502/m65xx.c sound/pokey.c
ARC_DEFS_milliped := ARC_VARIANT=1
ARC_SRC_arkanoid := drivers/arcades/arkanoid/arkanoid.c cpu/z80dsp/z80dsp.c sound/ay8910.c machine/taito68705dsp.c cpu/m6805dsp/m6805dsp.c
ARC_SRC_minivadr := drivers/arcades/minivadr/minivadr.c cpu/z80dsp/z80dsp.c
ARC_SRC_commando := drivers/arcades/commando/commando.c cpu/z80dsp/z80dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_gng := drivers/arcades/gng/gng.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_blktiger := drivers/arcades/blktiger/blktiger.c cpu/z80dsp/z80dsp.c cpu/mcs51dsp/mcs51dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_atetris := drivers/arcades/atetris/atetris.c cpu/m6502/m65xx.c machine/slapstic.c sound/pokey.c
ARC_SRC_bagman := drivers/arcades/bagman/bagman.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_SRC_ambush := drivers/arcades/ambush/ambush.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_SRC_blueprnt := drivers/arcades/blueprnt/blueprnt.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_SRC_btime := drivers/arcades/btime/btime.c cpu/m6502/m65xx.c sound/ay8910.c
ARC_SRC_shaolins := drivers/arcades/shaolins/shaolins.c cpu/m6809dsp/m6809dsp.c sound/sn76496.c
ARC_SRC_mikie := drivers/arcades/mikie/mikie.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/sn76496.c
ARC_SRC_galaxian := drivers/arcades/galaxian/galaxian.c cpu/z80dsp/z80dsp.c
ARC_SRC_galaga := drivers/arcades/galaga/galaga.c cpu/z80dsp/z80dsp.c cpu/mb88xx/mb88xx.c
ARC_SRC_pirates := drivers/arcades/pirates/pirates.c machine/eeprom93c46.c sound/okim6295dsp.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_blockout := drivers/arcades/blockout/blockout.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/okim6295dsp.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_bublbobl := drivers/arcades/bublbobl/bublbobl.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c sound/ym3812dsp.c
ARC_SRC_renegade := drivers/arcades/renegade/renegade.c cpu/m6502/m65xx.c cpu/m6809dsp/m6809dsp.c cpu/m6805dsp/m6805dsp.c machine/taito68705dsp.c sound/ym3812dsp.c sound/msm5205dsp.c
ARC_SRC_bankp := drivers/arcades/bankp/bankp.c cpu/z80dsp/z80dsp.c sound/sn76496.c
ARC_SRC_citycon := drivers/arcades/citycon/citycon.c cpu/m6809dsp/m6809dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c
ARC_SRC_brkthru := drivers/arcades/brkthru/brkthru.c cpu/m6809dsp/m6809dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c sound/ym3812dsp.c
ARC_SRC_appoooh := drivers/arcades/appoooh/appoooh.c cpu/z80dsp/z80dsp.c sound/sn76496.c sound/msm5205dsp.c
ARC_SRC_tehkanwc := drivers/arcades/tehkanwc/tehkanwc.c cpu/z80dsp/z80dsp.c sound/ay8910.c sound/msm5205dsp.c
ARC_SRC_tigerh := drivers/arcades/slapfght/slapfght.c cpu/z80dsp/z80dsp.c cpu/m6805dsp/m6805dsp.c machine/taito68705dsp.c sound/ay8910.c
ARC_SRC_slapfght := drivers/arcades/slapfght/slapfght.c cpu/z80dsp/z80dsp.c cpu/m6805dsp/m6805dsp.c machine/taito68705dsp.c sound/ay8910.c
ARC_DEFS_slapfght := ARC_VARIANT=1
ARC_SRC_wboy := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_SRC_pitfall2 := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_DEFS_pitfall2 := ARC_VARIANT=1
ARC_SRC_teddybb := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_DEFS_teddybb := ARC_VARIANT=2
ARC_SRC_mrviking := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_DEFS_mrviking := ARC_VARIANT=3
ARC_SRC_seganinj := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_DEFS_seganinj := ARC_VARIANT=4
ARC_SRC_upndown := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_DEFS_upndown := ARC_VARIANT=5
ARC_SRC_flicky := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_DEFS_flicky := ARC_VARIANT=6
ARC_SRC_gardia := drivers/arcades/system1/system1.c cpu/z80dsp/z80dsp.c machine/sega_decrypt.c machine/z80piodsp.c machine/i8255dsp.c sound/sn76496.c
ARC_DEFS_gardia := ARC_VARIANT=7
ARC_SRC_wwfsstar := drivers/arcades/wwfsstar/wwfsstar.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/okim6295dsp.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_bionicc := drivers/arcades/bionicc/bionicc.c cpu/z80dsp/z80dsp.c cpu/mcs51dsp/mcs51dsp.c sound/ym2151.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_shadoww := drivers/arcades/shadoww/shadoww.c cpu/z80dsp/z80dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c sound/okim6295dsp.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_chinagat := drivers/arcades/chinagat/chinagat.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/okim6295dsp.c
ARC_SRC_ddragon := drivers/arcades/ddragon/ddragon.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ym2151.c sound/msm5205dsp.c sound/okim6295dsp.c
ARC_SRC_ddragon2 := drivers/arcades/ddragon/ddragon.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ym2151.c sound/msm5205dsp.c sound/okim6295dsp.c
ARC_DEFS_ddragon2 := ARC_VARIANT=1
ARC_SRC_armedf := drivers/arcades/armedf/armedf.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_terraf := drivers/arcades/armedf/armedf.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c cpu/m68kdsp/m68kdsp.c
ARC_DEFS_terraf := ARC_VARIANT=1
ARC_SRC_cclimbr2 := drivers/arcades/armedf/armedf.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c cpu/m68kdsp/m68kdsp.c
ARC_DEFS_cclimbr2 := ARC_VARIANT=2
ARC_SRC_legion := drivers/arcades/armedf/armedf.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c cpu/m68kdsp/m68kdsp.c
ARC_DEFS_legion := ARC_VARIANT=3
ARC_SRC_kungfum := drivers/arcades/m62/m62.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ay8910.c sound/msm5205dsp.c
ARC_SRC_spelunkr := drivers/arcades/m62/m62.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ay8910.c sound/msm5205dsp.c
ARC_DEFS_spelunkr := ARC_VARIANT=1
ARC_SRC_spelunk2 := drivers/arcades/m62/m62.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ay8910.c sound/msm5205dsp.c
ARC_DEFS_spelunk2 := ARC_VARIANT=2
ARC_SRC_ldrun := drivers/arcades/m62/m62.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ay8910.c sound/msm5205dsp.c
ARC_DEFS_ldrun := ARC_VARIANT=3
ARC_SRC_ldrun2 := drivers/arcades/m62/m62.c cpu/z80dsp/z80dsp.c cpu/hd63701dsp/hd63701dsp.c sound/ay8910.c sound/msm5205dsp.c
ARC_DEFS_ldrun2 := ARC_VARIANT=4
ARC_SRC_rtype := drivers/arcades/m72/m72.c cpu/necv30/nec_v30.c cpu/z80dsp/z80dsp.c sound/ym2151.c
ARC_SRC_hharry := drivers/arcades/m72/m72.c cpu/necv30/nec_v30.c cpu/z80dsp/z80dsp.c sound/ym2151.c
ARC_DEFS_hharry := ARC_VARIANT=1
ARC_SRC_rtype2 := drivers/arcades/m72/m72.c cpu/necv30/nec_v30.c cpu/z80dsp/z80dsp.c sound/ym2151.c
ARC_DEFS_rtype2 := ARC_VARIANT=2
ARC_SRC_opwolf := drivers/arcades/opwolf/opwolf.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/msm5205dsp.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_lnc := drivers/arcades/btime/btime.c cpu/m6502/m65xx.c sound/ay8910.c
ARC_DEFS_lnc := ARC_VARIANT=1
ARC_SRC_mmonkey := drivers/arcades/btime/btime.c cpu/m6502/m65xx.c sound/ay8910.c
ARC_DEFS_mmonkey := ARC_VARIANT=2
ARC_SRC_darwin := drivers/arcades/brkthru/brkthru.c cpu/m6809dsp/m6809dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c sound/ym3812dsp.c
ARC_DEFS_darwin := ARC_VARIANT=1
ARC_SRC_saturnzi := drivers/arcades/blueprnt/blueprnt.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_saturnzi := ARC_VARIANT=1
ARC_SRC_grasspin := drivers/arcades/blueprnt/blueprnt.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_grasspin := ARC_VARIANT=2
ARC_SRC_robowres := drivers/arcades/appoooh/appoooh.c cpu/z80dsp/z80dsp.c sound/sn76496.c sound/msm5205dsp.c machine/sega_decrypt.c
ARC_DEFS_robowres := ARC_VARIANT=1
ARC_SRC_combh := drivers/arcades/bankp/bankp.c cpu/z80dsp/z80dsp.c sound/sn76496.c
ARC_DEFS_combh := ARC_VARIANT=1
ARC_SRC_mooncrst := drivers/arcades/galaxian/galaxian.c cpu/z80dsp/z80dsp.c
ARC_DEFS_mooncrst := ARC_VARIANT=1
ARC_SRC_scramble := drivers/arcades/galaxian/galaxian.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_scramble := ARC_VARIANT=2
ARC_SRC_frogger := drivers/arcades/galaxian/galaxian.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_frogger := ARC_VARIANT=3
ARC_SRC_cabal := drivers/arcades/seibu/cabal.c drivers/arcades/seibu/seibu_snd.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/msm5205dsp.c cpu/m68kdsp/m68kdsp.c
ARC_DEFS_cabal := SEIBU_ADPCM
ARC_SRC_bloodbro := drivers/arcades/seibu/bloodbros.c drivers/arcades/seibu/seibu_snd.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c sound/okim6295dsp.c cpu/m68kdsp/m68kdsp.c
ARC_DEFS_bloodbro := ARC_VARIANT=0
ARC_SRC_skysmash := drivers/arcades/seibu/bloodbros.c drivers/arcades/seibu/seibu_snd.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c sound/okim6295dsp.c cpu/m68kdsp/m68kdsp.c
ARC_DEFS_skysmash := ARC_VARIANT=1
ARC_SRC_arabian := drivers/arcades/arabian/arabian.c cpu/z80dsp/z80dsp.c cpu/mb88xx/mb88xx.c sound/ay8910.c
ARC_SRC_digdug := drivers/arcades/galaga/galaga.c cpu/z80dsp/z80dsp.c cpu/mb88xx/mb88xx.c
ARC_DEFS_digdug := ARC_VARIANT=1
ARC_SRC_xevious := drivers/arcades/galaga/galaga.c cpu/z80dsp/z80dsp.c cpu/mb88xx/mb88xx.c
ARC_DEFS_xevious := ARC_VARIANT=2
ARC_SRC_sxevious := drivers/arcades/galaga/galaga.c cpu/z80dsp/z80dsp.c cpu/mb88xx/mb88xx.c
ARC_DEFS_sxevious := ARC_VARIANT=3
ARC_SRC_bosco := drivers/arcades/galaga/galaga.c cpu/z80dsp/z80dsp.c cpu/mb88xx/mb88xx.c
ARC_DEFS_bosco := ARC_VARIANT=4
ARC_SRC_defender := drivers/arcades/williams/williams.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_DEFS_defender := ARC_VARIANT=0
ARC_SRC_mayday := drivers/arcades/williams/williams.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_DEFS_mayday := ARC_VARIANT=1
ARC_SRC_colony7 := drivers/arcades/williams/williams.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_DEFS_colony7 := ARC_VARIANT=2
ARC_SRC_joust := drivers/arcades/williams/williams.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_DEFS_joust := ARC_VARIANT=3
ARC_SRC_robotron := drivers/arcades/williams/williams.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_DEFS_robotron := ARC_VARIANT=4
ARC_SRC_stargate := drivers/arcades/williams/williams.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_DEFS_stargate := ARC_VARIANT=5
ARC_SRC_elevator := drivers/arcades/taitosj/taitosj.c cpu/z80dsp/z80dsp.c cpu/m6805dsp/m6805dsp.c sound/ay8910.c
ARC_SRC_junglek := drivers/arcades/taitosj/taitosj.c cpu/z80dsp/z80dsp.c cpu/m6805dsp/m6805dsp.c sound/ay8910.c
ARC_DEFS_junglek := ARC_VARIANT=1
ARC_SRC_tapper := drivers/arcades/mcr/mcr.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_tapper := ARC_VARIANT=0
ARC_SRC_tron := drivers/arcades/mcr/mcr.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_tron := ARC_VARIANT=1
ARC_SRC_shollow := drivers/arcades/mcr/mcr.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_shollow := ARC_VARIANT=2
ARC_SRC_domino := drivers/arcades/mcr/mcr.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_domino := ARC_VARIANT=3
ARC_SRC_wacko := drivers/arcades/mcr/mcr.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_wacko := ARC_VARIANT=4
ARC_SRC_dotron := drivers/arcades/mcr/mcr.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_dotron := ARC_VARIANT=5
ARC_SRC_timber := drivers/arcades/mcr/mcr.c cpu/z80dsp/z80dsp.c sound/ay8910.c
ARC_DEFS_timber := ARC_VARIANT=6
ARC_SRC_depthch := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_depthch := ARC_VARIANT=0
ARC_SRC_safari := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_safari := ARC_VARIANT=1
ARC_SRC_frogs := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_frogs := ARC_VARIANT=2
ARC_SRC_sspaceat := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_sspaceat := ARC_VARIANT=3
ARC_SRC_sspacaho := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_sspacaho := ARC_VARIANT=4
ARC_SRC_headon := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_headon := ARC_VARIANT=5
ARC_SRC_headon2 := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_headon2 := ARC_VARIANT=6
ARC_SRC_headon2s := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_headon2s := ARC_VARIANT=7
ARC_SRC_invho2 := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_invho2 := ARC_VARIANT=8
ARC_SRC_nsub := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_nsub := ARC_VARIANT=9
ARC_SRC_samurai := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_samurai := ARC_VARIANT=10
ARC_SRC_invinco := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_invinco := ARC_VARIANT=11
ARC_SRC_invds := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_invds := ARC_VARIANT=12
ARC_SRC_tranqgun := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_tranqgun := ARC_VARIANT=13
ARC_SRC_spacetrk := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_spacetrk := ARC_VARIANT=14
ARC_SRC_carnival := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c cpu/mcs48/mcs48.c sound/ay8910.c
ARC_DEFS_carnival := ARC_VARIANT=15
ARC_SRC_brdrline := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_brdrline := ARC_VARIANT=16
ARC_SRC_digger := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_digger := ARC_VARIANT=17
ARC_SRC_pulsar := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_pulsar := ARC_VARIANT=18
ARC_SRC_heiankyo := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_heiankyo := ARC_VARIANT=19
ARC_SRC_alphaho := drivers/arcades/vicdual/vicdual.c cpu/z80dsp/z80dsp.c
ARC_DEFS_alphaho := ARC_VARIANT=20
ARC_SRC_asteroid := drivers/arcades/asteroid/asteroid.c cpu/m6502/m65xx.c
ARC_SRC_baraduke := drivers/arcades/baraduke/baraduke.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_SRC_metrocrs := drivers/arcades/baraduke/baraduke.c cpu/m6809dsp/m6809dsp.c cpu/hd63701dsp/hd63701dsp.c
ARC_DEFS_metrocrs := ARC_VARIANT=1
ARC_SRC_punchout := drivers/arcades/punchout/punchout.c cpu/z80dsp/z80dsp.c cpu/m6502/m65xx.c sound/nes_apu.c sound/vlm5030dsp.c
ARC_SRC_shuuz := drivers/arcades/shuuz/shuuz.c video/atari_mo.c sound/okim6295dsp.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_skullxbo := drivers/arcades/skullxbo/skullxbo.c video/atari_mo.c sound/okim6295dsp.c sound/ym2151.c cpu/m6502/m65xx.c cpu/m68kdsp/m68kdsp.c
ARC_SRC_ikari := drivers/arcades/snk/snk.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c
ARC_DEFS_ikari := ARC_VARIANT=0
ARC_SRC_athena := drivers/arcades/snk/snk.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c
ARC_DEFS_athena := ARC_VARIANT=1
ARC_SRC_tnk3 := drivers/arcades/snk/snk.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c
ARC_DEFS_tnk3 := ARC_VARIANT=2
ARC_SRC_aso := drivers/arcades/snk/snk.c cpu/z80dsp/z80dsp.c sound/ym3812dsp.c
ARC_DEFS_aso := ARC_VARIANT=3
ARC_SRC_actfancr := drivers/arcades/actfancr/actfancr.c cpu/hu6280dsp/hu6280dsp.c cpu/m6502/m65xx.c sound/ym3812dsp.c sound/ym2203dsp.c sound/fmopn.c sound/ay8910.c sound/okim6295dsp.c video/deco_bac06d.c
ARC_SRC_sentetst := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_sentetst := ARC_VARIANT=0
ARC_SRC_cshift := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_cshift := ARC_VARIANT=1
ARC_SRC_hattrick := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_hattrick := ARC_VARIANT=2
ARC_SRC_gghost := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_gghost := ARC_VARIANT=3
ARC_SRC_otwalls := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_otwalls := ARC_VARIANT=4
ARC_SRC_snakepit := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_snakepit := ARC_VARIANT=5
ARC_SRC_triviag1 := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_triviag1 := ARC_VARIANT=6
ARC_SRC_snakjack := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_snakjack := ARC_VARIANT=7
ARC_SRC_stocker := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_stocker := ARC_VARIANT=8
ARC_SRC_triviabb := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_triviabb := ARC_VARIANT=9
ARC_SRC_triviag2 := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_triviag2 := ARC_VARIANT=10
ARC_SRC_triviayp := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_triviayp := ARC_VARIANT=11
ARC_SRC_triviasp := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_triviasp := ARC_VARIANT=12
ARC_SRC_gimeabrk := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_gimeabrk := ARC_VARIANT=13
ARC_SRC_minigolf := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_minigolf := ARC_VARIANT=14
ARC_SRC_teamht := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_teamht := ARC_VARIANT=15
ARC_SRC_grudge := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_grudge := ARC_VARIANT=16
ARC_SRC_triviaes := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_triviaes := ARC_VARIANT=17
ARC_SRC_toggle := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_toggle := ARC_VARIANT=18
ARC_SRC_nstocker := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_nstocker := ARC_VARIANT=19
ARC_SRC_sfootbal := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_sfootbal := ARC_VARIANT=20
ARC_SRC_spiker := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_spiker := ARC_VARIANT=21
ARC_SRC_stompin := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_stompin := ARC_VARIANT=22
ARC_SRC_nametune := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_nametune := ARC_VARIANT=23
ARC_SRC_rescraid := drivers/arcades/balsente/balsente.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/cem3394d.c
ARC_DEFS_rescraid := ARC_VARIANT=24
ARC_SRC_aliens := drivers/arcades/konami/aliens.c cpu/konamidsp/konamidsp.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/k007232d.c video/k052109d.c video/k051960d.c
ARC_SRC_ajax := drivers/arcades/konami/ajax.c cpu/konamidsp/konamidsp.c cpu/m6809dsp/m6809dsp.c cpu/z80dsp/z80dsp.c sound/ym2151.c sound/k007232d.c video/k052109d.c video/k051960d.c video/k051316d.c
ARC_SRC_simpsons := drivers/arcades/konami/simpsons.c cpu/konamidsp/konamidsp.c cpu/z80dsp/z80dsp.c sound/ym2151.c video/k052109d.c
ARC_RAM_bombjack := z80dsp.c.o bombjack.c.o ay8910.c.o
ARC_RAM_calorie := z80dsp.c.o bombjack.c.o ay8910.c.o
ARC_RAM_mrdo := z80dsp.c.o mrdo.c.o sn76496.c.o
ARC_RAM_centiped := m65xx.c.o centipede.c.o pokey.c.o
ARC_RAM_milliped := m65xx.c.o centipede.c.o pokey.c.o
ARC_RAM_arkanoid := z80dsp.c.o arkanoid.c.o ay8910.c.o m6805dsp.c.o taito68705dsp.c.o
ARC_RAM_minivadr := z80dsp.c.o minivadr.c.o
ARC_RAM_commando := z80dsp.c.o commando.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_gng := m6809dsp.c.o z80dsp.c.o gng.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_blktiger := z80dsp.c.o mcs51dsp.c.o blktiger.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_wwfsstar := m68kdsp.c.o z80dsp.c.o wwfsstar.c.o ym2151.c.o okim6295dsp.c.o
ARC_RAM_bionicc := m68kdsp.c.o z80dsp.c.o mcs51dsp.c.o bionicc.c.o ym2151.c.o
ARC_RAM_atetris := m65xx.c.o atetris.c.o pokey.c.o slapstic.c.o
ARC_RAM_bagman := z80dsp.c.o bagman.c.o ay8910.c.o
ARC_RAM_ambush := z80dsp.c.o ambush.c.o ay8910.c.o
ARC_RAM_blueprnt := z80dsp.c.o blueprnt.c.o ay8910.c.o
ARC_RAM_btime := m65xx.c.o btime.c.o ay8910.c.o
ARC_RAM_shaolins := m6809dsp.c.o shaolins.c.o sn76496.c.o
ARC_RAM_mikie := m6809dsp.c.o z80dsp.c.o mikie.c.o sn76496.c.o
ARC_RAM_galaxian := z80dsp.c.o galaxian.c.o
ARC_RAM_galaga := z80dsp.c.o galaga.c.o
ARC_RAM_pirates := m68kdsp.c.o pirates.c.o okim6295dsp.c.o
ARC_RAM_blockout := m68kdsp.c.o z80dsp.c.o blockout.c.o ym2151.c.o okim6295dsp.c.o
ARC_RAM_bublbobl := z80dsp.c.o hd63701dsp.c.o bublbobl.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o ym3812dsp.c.o
ARC_RAM_renegade := m65xx.c.o m6809dsp.c.o m6805dsp.c.o taito68705dsp.c.o renegade.c.o ym3812dsp.c.o
ARC_RAM_bankp := z80dsp.c.o bankp.c.o sn76496.c.o
ARC_RAM_citycon := m6809dsp.c.o citycon.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o
ARC_RAM_brkthru := m6809dsp.c.o brkthru.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o ym3812dsp.c.o
ARC_RAM_appoooh := z80dsp.c.o appoooh.c.o sn76496.c.o msm5205dsp.c.o
ARC_RAM_tehkanwc := z80dsp.c.o tehkanwc.c.o ay8910.c.o msm5205dsp.c.o
ARC_RAM_tigerh := z80dsp.c.o m6805dsp.c.o taito68705dsp.c.o slapfght.c.o ay8910.c.o
ARC_RAM_slapfght := z80dsp.c.o m6805dsp.c.o taito68705dsp.c.o slapfght.c.o ay8910.c.o
ARC_RAM_wboy := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_pitfall2 := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_teddybb := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_mrviking := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_seganinj := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_upndown := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_flicky := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_gardia := z80dsp.c.o system1.c.o z80piodsp.c.o i8255dsp.c.o sn76496.c.o
ARC_RAM_shadoww := m68kdsp.c.o z80dsp.c.o shadoww.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o okim6295dsp.c.o
ARC_RAM_chinagat := m6809dsp.c.o z80dsp.c.o chinagat.c.o ym2151.c.o okim6295dsp.c.o
ARC_RAM_ddragon := m6809dsp.c.o z80dsp.c.o hd63701dsp.c.o ddragon.c.o ym2151.c.o msm5205dsp.c.o okim6295dsp.c.o
ARC_RAM_ddragon2 := m6809dsp.c.o z80dsp.c.o hd63701dsp.c.o ddragon.c.o ym2151.c.o msm5205dsp.c.o okim6295dsp.c.o
ARC_RAM_armedf := m68kdsp.c.o z80dsp.c.o armedf.c.o ym3812dsp.c.o
ARC_RAM_terraf := m68kdsp.c.o z80dsp.c.o armedf.c.o ym3812dsp.c.o
ARC_RAM_cclimbr2 := m68kdsp.c.o z80dsp.c.o armedf.c.o ym3812dsp.c.o
ARC_RAM_legion := m68kdsp.c.o z80dsp.c.o armedf.c.o ym3812dsp.c.o
ARC_RAM_kungfum := z80dsp.c.o hd63701dsp.c.o m62.c.o ay8910.c.o msm5205dsp.c.o
ARC_RAM_spelunkr := z80dsp.c.o hd63701dsp.c.o m62.c.o ay8910.c.o msm5205dsp.c.o
ARC_RAM_spelunk2 := z80dsp.c.o hd63701dsp.c.o m62.c.o ay8910.c.o msm5205dsp.c.o
ARC_RAM_ldrun := z80dsp.c.o hd63701dsp.c.o m62.c.o ay8910.c.o msm5205dsp.c.o
ARC_RAM_ldrun2 := z80dsp.c.o hd63701dsp.c.o m62.c.o ay8910.c.o msm5205dsp.c.o
ARC_RAM_rtype := nec_v30.c.o z80dsp.c.o m72.c.o ym2151.c.o
ARC_RAM_hharry := nec_v30.c.o z80dsp.c.o m72.c.o ym2151.c.o
ARC_RAM_rtype2 := nec_v30.c.o z80dsp.c.o m72.c.o ym2151.c.o
ARC_RAM_opwolf := m68kdsp.c.o z80dsp.c.o opwolf.c.o ym2151.c.o msm5205dsp.c.o
ARC_RAM_lnc := m65xx.c.o btime.c.o ay8910.c.o
ARC_RAM_mmonkey := m65xx.c.o btime.c.o ay8910.c.o
ARC_RAM_darwin := m6809dsp.c.o brkthru.c.o ym2203dsp.c.o fmopn.c.o ay8910.c.o ym3812dsp.c.o
ARC_RAM_saturnzi := z80dsp.c.o blueprnt.c.o ay8910.c.o
ARC_RAM_grasspin := z80dsp.c.o blueprnt.c.o ay8910.c.o
ARC_RAM_robowres := z80dsp.c.o appoooh.c.o sn76496.c.o msm5205dsp.c.o
ARC_RAM_combh := z80dsp.c.o bankp.c.o sn76496.c.o
ARC_RAM_mooncrst := z80dsp.c.o galaxian.c.o
ARC_RAM_scramble := z80dsp.c.o galaxian.c.o ay8910.c.o
ARC_RAM_frogger := z80dsp.c.o galaxian.c.o ay8910.c.o
ARC_RAM_shuuz := m68kdsp.c.o shuuz.c.o atari_mo.c.o okim6295dsp.c.o
ARC_RAM_skullxbo := m68kdsp.c.o skullxbo.c.o atari_mo.c.o okim6295dsp.c.o ym2151.c.o m65xx.c.o
ARC_RAM_cabal := m68kdsp.c.o z80dsp.c.o cabal.c.o seibu_snd.c.o ym2151.c.o msm5205dsp.c.o
ARC_RAM_bloodbro := m68kdsp.c.o z80dsp.c.o bloodbros.c.o seibu_snd.c.o ym3812dsp.c.o okim6295dsp.c.o
ARC_RAM_skysmash := m68kdsp.c.o z80dsp.c.o bloodbros.c.o seibu_snd.c.o ym3812dsp.c.o okim6295dsp.c.o
ARC_RAM_arabian := z80dsp.c.o arabian.c.o mb88xx.c.o ay8910.c.o
ARC_RAM_digdug := z80dsp.c.o galaga.c.o mb88xx.c.o
ARC_RAM_xevious := z80dsp.c.o galaga.c.o mb88xx.c.o
ARC_RAM_sxevious := z80dsp.c.o galaga.c.o mb88xx.c.o
ARC_RAM_bosco := z80dsp.c.o galaga.c.o mb88xx.c.o
ARC_RAM_defender := m6809dsp.c.o williams.c.o hd63701dsp.c.o
ARC_RAM_mayday := m6809dsp.c.o williams.c.o hd63701dsp.c.o
ARC_RAM_colony7 := m6809dsp.c.o williams.c.o hd63701dsp.c.o
ARC_RAM_joust := m6809dsp.c.o williams.c.o hd63701dsp.c.o
ARC_RAM_robotron := m6809dsp.c.o williams.c.o hd63701dsp.c.o
ARC_RAM_stargate := m6809dsp.c.o williams.c.o hd63701dsp.c.o
ARC_RAM_elevator := z80dsp.c.o taitosj.c.o m6805dsp.c.o ay8910.c.o
ARC_RAM_junglek := z80dsp.c.o taitosj.c.o m6805dsp.c.o ay8910.c.o
ARC_RAM_tapper := z80dsp.c.o mcr.c.o ay8910.c.o
ARC_RAM_tron := z80dsp.c.o mcr.c.o ay8910.c.o
ARC_RAM_shollow := z80dsp.c.o mcr.c.o ay8910.c.o
ARC_RAM_domino := z80dsp.c.o mcr.c.o ay8910.c.o
ARC_RAM_wacko := z80dsp.c.o mcr.c.o ay8910.c.o
ARC_RAM_dotron := z80dsp.c.o mcr.c.o ay8910.c.o
ARC_RAM_timber := z80dsp.c.o mcr.c.o ay8910.c.o
ARC_RAM_depthch := z80dsp.c.o vicdual.c.o
ARC_RAM_safari := z80dsp.c.o vicdual.c.o
ARC_RAM_frogs := z80dsp.c.o vicdual.c.o
ARC_RAM_sspaceat := z80dsp.c.o vicdual.c.o
ARC_RAM_sspacaho := z80dsp.c.o vicdual.c.o
ARC_RAM_headon := z80dsp.c.o vicdual.c.o
ARC_RAM_headon2 := z80dsp.c.o vicdual.c.o
ARC_RAM_headon2s := z80dsp.c.o vicdual.c.o
ARC_RAM_invho2 := z80dsp.c.o vicdual.c.o
ARC_RAM_nsub := z80dsp.c.o vicdual.c.o
ARC_RAM_samurai := z80dsp.c.o vicdual.c.o
ARC_RAM_invinco := z80dsp.c.o vicdual.c.o
ARC_RAM_invds := z80dsp.c.o vicdual.c.o
ARC_RAM_tranqgun := z80dsp.c.o vicdual.c.o
ARC_RAM_spacetrk := z80dsp.c.o vicdual.c.o
ARC_RAM_carnival := z80dsp.c.o vicdual.c.o mcs48.c.o ay8910.c.o
ARC_RAM_brdrline := z80dsp.c.o vicdual.c.o
ARC_RAM_digger := z80dsp.c.o vicdual.c.o
ARC_RAM_pulsar := z80dsp.c.o vicdual.c.o
ARC_RAM_heiankyo := z80dsp.c.o vicdual.c.o
ARC_RAM_alphaho := z80dsp.c.o vicdual.c.o
ARC_RAM_asteroid := m65xx.c.o asteroid.c.o
ARC_RAM_baraduke := m6809dsp.c.o hd63701dsp.c.o baraduke.c.o
ARC_RAM_metrocrs := m6809dsp.c.o hd63701dsp.c.o baraduke.c.o
ARC_RAM_punchout := z80dsp.c.o m65xx.c.o nes_apu.c.o vlm5030dsp.c.o punchout.c.o
ARC_RAM_ikari := z80dsp.c.o snk.c.o ym3812dsp.c.o
ARC_RAM_athena := z80dsp.c.o snk.c.o ym3812dsp.c.o
ARC_RAM_tnk3 := z80dsp.c.o snk.c.o ym3812dsp.c.o
ARC_RAM_aso := z80dsp.c.o snk.c.o ym3812dsp.c.o
ARC_RAM_actfancr := hu6280dsp.c.o m65xx.c.o actfancr.c.o deco_bac06d.c.o ym3812dsp.c.o ym2203dsp.c.o fmopn.c.o okim6295dsp.c.o
ARC_RAM_sentetst := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_cshift := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_hattrick := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_gghost := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_otwalls := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_snakepit := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_triviag1 := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_snakjack := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_stocker := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_triviabb := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_triviag2 := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_triviayp := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_triviasp := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_gimeabrk := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_minigolf := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_teamht := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_grudge := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_triviaes := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_toggle := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_nstocker := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_sfootbal := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_spiker := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_stompin := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_nametune := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_rescraid := m6809dsp.c.o z80dsp.c.o balsente.c.o cem3394d.c.o
ARC_RAM_aliens := konamidsp.c.o z80dsp.c.o aliens.c.o ym2151.c.o k007232d.c.o k052109d.c.o k051960d.c.o
ARC_RAM_ajax := konamidsp.c.o m6809dsp.c.o z80dsp.c.o ajax.c.o ym2151.c.o k007232d.c.o k052109d.c.o k051960d.c.o k051316d.c.o
ARC_RAM_simpsons := konamidsp.c.o z80dsp.c.o simpsons.c.o ym2151.c.o k052109d.c.o
