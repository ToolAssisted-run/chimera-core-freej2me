import java.io.ByteArrayInputStream;
import java.util.Timer;
import java.util.TimerTask;
import javax.microedition.lcdui.*;
import javax.microedition.lcdui.game.GameCanvas;
import javax.microedition.media.Manager;
import javax.microedition.media.Player;
import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.RecordStore;

/* The test MIDlet of the FreeJ2ME core: a square moved by the keys, a timer
 * ticking every 50 ms, text, a score saved to a record store every 30
 * frames, a touch that leaves a mark, Japanese text (the core's fallback
 * font), Chinese text read in the machine's encoding, and sound - a MIDI tune that loops,
 * a tone on Fire and a beep (a WAV made here) on 5. Everything it draws and
 * plays depends on the keys and the machine's clock, nothing on the host.
 * SPDX-License-Identifier: MIT */
public class TestMIDlet extends MIDlet {
    /* the four characters of "Chinese test", in GBK, read with the machine's text
     * encoding: Chinese characters when it is GBK, other ones when it is not */
    static final byte[] CHINESE = { (byte) 0xd6, (byte) 0xd0, (byte) 0xce, (byte) 0xc4, (byte) 0xb2, (byte) 0xe2, (byte) 0xca, (byte) 0xd4 };

    private Game game;

    protected void startApp() {
        if (game == null) {
            game = new Game();
            Display.getDisplay(this).setCurrent(game);
            new Thread(game).start();
        }
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) { }

    /* a MIDI file: one track, middle C to the octave and back, a note every
     * quarter of a second */
    static byte[] tune() {
        int[] notes = { 60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62 };
        java.io.ByteArrayOutputStream t = new java.io.ByteArrayOutputStream();
        t.write(0); t.write(0xc0); t.write(80);                 // program: square lead
        for (int i = 0; i < notes.length; i++) {
            t.write(0); t.write(0x90); t.write(notes[i]); t.write(100);
            t.write(48); t.write(0x80); t.write(notes[i]); t.write(0);
        }
        t.write(0); t.write(0xff); t.write(0x2f); t.write(0);   // end of track
        byte[] track = t.toByteArray();
        byte[] head = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
                        'M', 'T', 'r', 'k', 0, 0, (byte)(track.length >> 8), (byte)track.length };
        byte[] all = new byte[head.length + track.length];
        System.arraycopy(head, 0, all, 0, head.length);
        System.arraycopy(track, 0, all, head.length, track.length);
        return all;
    }

    /* a WAV file: 0.1 s of a 1 kHz square wave, 8 kHz, 8-bit mono */
    static byte[] beep() {
        int n = 800;
        byte[] w = new byte[44 + n];
        String riff = "RIFF....WAVEfmt ";
        for (int i = 0; i < 16; i++) w[i] = (byte)riff.charAt(i);
        int[] le = { 4, 36 + n, 16, 16, 20, 1 | (1 << 16), 24, 8000, 28, 8000, 32, 1 | (8 << 16), 40, n };
        for (int i = 0; i < le.length; i += 2)
            for (int b = 0; b < 4; b++) w[le[i] + b] = (byte)(le[i + 1] >> (8 * b));
        String data = "data";
        for (int i = 0; i < 4; i++) w[36 + i] = (byte)data.charAt(i);
        for (int i = 0; i < n; i++) w[44 + i] = (byte)((i / 4) % 2 == 0 ? 0xc0 : 0x40);
        return w;
    }

    static class Game extends GameCanvas implements Runnable {
        int x = 100, y = 100, frames, ticks, saved, loaded = -1, tx = -1, ty = -1;
        int[] trail = new int[64];
        Player music, sound;
        String audio = "";

        Game() {
            super(false);
            // the score a saved game left (the core's save data slot)
            try {
                RecordStore rs = RecordStore.openRecordStore("score", false);
                loaded = Integer.parseInt(new String(rs.getRecord(1)));
                rs.closeRecordStore();
            } catch (Exception e) {
                loaded = -1;
            }
            new Timer().schedule(new TimerTask() {
                public void run() { ticks++; }
            }, 0, 50);
            try {
                music = Manager.createPlayer(new ByteArrayInputStream(tune()), "audio/midi");
                music.setLoopCount(-1);
                music.start();
                sound = Manager.createPlayer(new ByteArrayInputStream(beep()), "audio/x-wav");
                sound.realize();
                sound.prefetch();
                audio = "sound";
            } catch (Exception e) {
                audio = "no sound: " + e;
            }
        }

        public void pointerPressed(int px, int py) { tx = px; ty = py; }
        public void pointerDragged(int px, int py) { tx = px; ty = py; }

        public void keyPressed(int code) {
            try {
                if (code == FIRE || getGameAction(code) == FIRE) Manager.playTone(72 + (frames % 12), 100, 80);
                if (code == KEY_NUM5) { sound.stop(); sound.setMediaTime(0); sound.start(); }
            } catch (Exception e) {
                audio = "no tone: " + e;
            }
        }

        public void run() {
            Graphics g = getGraphics();
            for (;;) {
                int k = getKeyStates();
                if ((k & LEFT_PRESSED) != 0) x -= 3;
                if ((k & RIGHT_PRESSED) != 0) x += 3;
                if ((k & UP_PRESSED) != 0) y -= 3;
                if ((k & DOWN_PRESSED) != 0) y += 3;
                if ((k & FIRE_PRESSED) != 0) trail[frames & 63] = x * 1000 + y;
                g.setColor(0x102040);
                g.fillRect(0, 0, getWidth(), getHeight());
                g.setColor(0xffcc00);
                g.fillRect(x, y, 16, 16);
                g.setColor(0xffffff);
                g.drawString("frame " + frames + " tick " + ticks, 2, 2, Graphics.TOP | Graphics.LEFT);
                g.drawString("saved " + saved, 2, 20, Graphics.TOP | Graphics.LEFT);
                g.drawString(audio, 2, 38, Graphics.TOP | Graphics.LEFT);
                g.drawString("\u65e5\u672c\u8a9e \u30c6\u30b9\u30c8", 2, 56, Graphics.TOP | Graphics.LEFT);
                g.drawString(loaded < 0 ? "nothing loaded" : "loaded " + loaded, 2, 74, Graphics.TOP | Graphics.LEFT);
                g.drawString(new String(CHINESE), 2, 92, Graphics.TOP | Graphics.LEFT);
                for (int i = 0; i < 64; i++)
                    if (trail[i] != 0) g.drawRect(trail[i] / 1000, trail[i] % 1000, 3, 3);
                if (tx >= 0) { g.setColor(0x40ff40); g.fillArc(tx - 5, ty - 5, 10, 10, 0, 360); }
                flushGraphics();
                frames++;
                if (frames % 30 == 0) save();
                try { Thread.sleep(33); } catch (InterruptedException e) { }
            }
        }

        void save() {
            try {
                RecordStore rs = RecordStore.openRecordStore("score", true);
                byte[] b = Integer.toString(frames * 7 + x).getBytes();
                if (rs.getNumRecords() == 0) rs.addRecord(b, 0, b.length);
                else rs.setRecord(1, b, 0, b.length);
                saved = Integer.parseInt(new String(rs.getRecord(1)));
                rs.closeRecordStore();
            } catch (Exception e) {
                saved = -1;
            }
        }
    }
}
