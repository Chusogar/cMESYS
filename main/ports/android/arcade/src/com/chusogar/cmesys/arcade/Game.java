package com.chusogar.cmesys.arcade;

/** The arcade games of cMESYS (main/drivers/arcades), each one a library
 *  (libgame_<id>.so), with what the controls show for it: its buttons (the
 *  desktop frontend's B1..B3, labelled for the game) and how its joystick
 *  moves. */
final class Game {
    static final int JOY_8WAY = 0, JOY_2WAY = 1, JOY_NONE = 2;

    final String id, title, maker;
    final int year, joystick;
    final String[] buttons;       // labels, in the order shown
    final int[] buttonBits;       // Native.B1..B3 of each
    final int color;              // the list's colour for it

    private Game(String id, String title, String maker, int year, int joystick, String[] buttons, int[] bits, int color) {
        this.id = id;
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
        new Game("trackfld", "Track & Field", "Konami", 1983, JOY_NONE,
                new String[] {"CORRER", "SALTO", "CORRER"}, new int[] {B3, B2, B1}, 0xffc08a1a),
    };

    static Game find(String id) {
        for (Game g : ALL) if (g.id.equals(id)) return g;
        return null;
    }
}
