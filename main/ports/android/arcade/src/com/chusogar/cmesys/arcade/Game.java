package com.chusogar.cmesys.arcade;

/** The arcade games of cMESYS (main/drivers/arcades), each one a library
 *  (libgame_<id>.so), with what the controls show for it: its buttons (the
 *  desktop frontend's B1..B3, labelled for the game) and how its joystick
 *  moves. */
final class Game {
    static final int JOY_8WAY = 0, JOY_2WAY = 1, JOY_NONE = 2, JOY_YOKE = 3;

    final String id, title, maker;
    final String lib;             // its library (libgame_<lib>.so)
    final String[] args;          // what the library's main is run with
    final int year, joystick;
    final String[] buttons;       // labels, in the order shown
    final int[] buttonBits;       // Native.B1..B3 of each
    final int color;              // the list's colour for it

    private Game(String id, String title, String maker, int year, int joystick, String[] buttons, int[] bits, int color) {
        this(id, id, new String[0], title, maker, year, joystick, buttons, bits, color);
    }

    private Game(String id, String lib, String[] args, String title, String maker, int year, int joystick,
            String[] buttons, int[] bits, int color) {
        this.id = id;
        this.lib = lib;
        this.args = args;
        this.title = title;
        this.maker = maker;
        this.year = year;
        this.joystick = joystick;
        this.buttons = buttons;
        this.buttonBits = bits;
        this.color = color;
    }

    private static final int B1 = Native.B1, B2 = Native.B2, B3 = Native.B3;

    static final Game[] ALL = {
        // the arcades on the common frontend (main/drivers/arcades/arcades.mk)
        new Game("arkanoid", "Arkanoid", "Taito", 1986, JOY_2WAY,
                new String[] {"DISPARO"}, new int[] {B1}, 0xff3a4aa0),
        new Game("bombjack", "Bomb Jack", "Tehkan", 1984, JOY_8WAY,
                new String[] {"SALTO"}, new int[] {B1}, 0xffc04a3a),
        new Game("calorie", "Calorie Kun vs Moguranian", "Sega", 1986, JOY_8WAY,
                new String[] {"PUÑETAZO", "SALTO"}, new int[] {B1, B2}, 0xffd08a3a),
        new Game("centiped", "Centipede", "Atari", 1980, JOY_8WAY,
                new String[] {"DISPARO"}, new int[] {B1}, 0xff4aa04a),
        new Game("milliped", "Millipede", "Atari", 1982, JOY_8WAY,
                new String[] {"DISPARO"}, new int[] {B1}, 0xff8a4ac0),
        new Game("minivadr", "Mini Vaders", "Taito", 1990, JOY_2WAY,
                new String[] {"DISPARO"}, new int[] {B1}, 0xff5a5a5a),
        new Game("mrdo", "Mr. Do!", "Universal", 1982, JOY_8WAY,
                new String[] {"BOLA"}, new int[] {B1}, 0xffd03a5a),
        // one library for both (sw_core.c), the flight yoke of the cabinet
        new Game("esb", "starwars", new String[] {"esb"}, "The Empire Strikes Back", "Atari", 1985, JOY_YOKE,
                new String[] {"GATILLO", "PULGAR"}, new int[] {B1, B2}, 0xff3a5a9a),
        new Game("gauntlet", "Gauntlet", "Atari Games", 1985, JOY_8WAY,
                new String[] {"FUEGO", "MAGIA"}, new int[] {B1, B2}, 0xff8a6a2a),
        new Game("indytemp", "Indiana Jones and the Temple of Doom", "Atari Games", 1985, JOY_8WAY,
                new String[] {"LÁTIGO", "SALTO"}, new int[] {B1, B2}, 0xff9a4a22),
        new Game("outrun", "Out Run", "Sega", 1986, JOY_8WAY,
                new String[] {"ACELERAR", "FRENO", "MARCHA"}, new int[] {B1, B2, B3}, 0xff2a6ab0),
        new Game("pbobble", "Puzzle Bobble", "Taito", 1994, JOY_8WAY,
                new String[] {"DISPARO"}, new int[] {B1}, 0xff3a9a5a),
        new Game("polepos", "Pole Position", "Namco", 1982, JOY_8WAY,
                new String[] {"ACELERAR", "FRENO", "MARCHA"}, new int[] {B1, B2, B3}, 0xffb03030),
        new Game("polepos2", "Pole Position II", "Namco", 1983, JOY_8WAY,
                new String[] {"ACELERAR", "FRENO", "MARCHA"}, new int[] {B1, B2, B3}, 0xffc04a2a),
        new Game("retofinv", "Return of the Invaders", "Taito", 1985, JOY_2WAY,
                new String[] {"DISPARO"}, new int[] {B1}, 0xff5a3ab0),
        new Game("roadrunn", "Road Runner", "Atari Games", 1985, JOY_8WAY,
                new String[] {"SALTO"}, new int[] {B1}, 0xff3a7ac0),
        new Game("robocop", "RoboCop", "Data East", 1988, JOY_8WAY,
                new String[] {"DISPARO", "SALTO"}, new int[] {B1, B2}, 0xff6a7a8a),
        new Game("snowbros", "Snow Bros.", "Toaplan", 1990, JOY_8WAY,
                new String[] {"NIEVE", "SALTO"}, new int[] {B1, B2}, 0xff2a8ac0),
        // the cabinet's RUN - JUMP - RUN (the desktop's C = run 1, Z = run 2, X = jump)
        new Game("starwars", "starwars", new String[] {"starwars"}, "Star Wars", "Atari", 1983, JOY_YOKE,
                new String[] {"GATILLO", "PULGAR"}, new int[] {B1, B2}, 0xffd4a020),
        new Game("trackfld", "Track & Field", "Konami", 1983, JOY_NONE,
                new String[] {"CORRER", "SALTO", "CORRER"}, new int[] {B3, B2, B1}, 0xffc08a1a),
    };

    static {   // the list by title
        java.util.Arrays.sort(ALL, new java.util.Comparator<Game>() {
            @Override public int compare(Game a, Game b) { return a.title.compareToIgnoreCase(b.title); }
        });
    }

    static Game find(String id) {
        for (Game g : ALL) if (g.id.equals(id)) return g;
        return null;
    }
}
