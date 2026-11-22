/*
	The Chimera front end for FreeJ2ME-Plus, modelled on its Libretro.java.
	GPL-3.0-or-later, like FreeJ2ME-Plus.

	The core calls start() once and frame() once a frame, through JNI, and
	reads the picture and the sound from the fields below. Nothing here calls
	into the core: the core only calls Java, as the java launcher does.

	A frame is 1/60 s of the machine's time. frame() hands FreeJ2ME the
	keys and the touch that changed, sleeps for that 1/60 s while the game's
	threads run, then copies the picture and takes 735 samples of sound.
*/
package chimera;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import javax.microedition.media.Manager;

import org.recompile.mobile.Mobile;
import org.recompile.mobile.MobilePlatform;

import chimera.sound.Bus;

public final class Frontend
{
	/* the picture at the end of the last frame, 0xAARRGGBB, width x height */
	public static int[] lcd;
	public static int width, height;
	/* the last frame's sound: 735 frames of interleaved 16-bit stereo */
	public static final int AUDIO_FRAMES = Bus.RATE / 60;
	public static final short[] audio = new short[2 * AUDIO_FRAMES];
	/* why start() failed */
	public static String error = "";
	/* A text a person reads is a key of the core's languages/en.json, each of
	   its arguments after a TEXT_ARG (chimera docs/translations.md). */
	static final String TEXT_ARG = "\u001f";
	/* whether the game gave the screen a new picture in the last frame: a
	   frame without one is a lag frame (chimera#301) */
	public static boolean newPicture;
	/* pictures given so far: FreeJ2ME calls the painter once for each */
	private static volatile int pictures;

	private static final long FRAME_NANOS = 16_666_667L;

	private static int[] front;
	private static int held, lastPressedKey = -1;
	private static boolean touching;
	private static int touchX, touchY;

	/* game: the file to load. settings: FreeJ2ME's own setting names and
	   values, "name=value", set before the game runs as the libretro front
	   end sets them; a name starting with "sys." is a system setting, and
	   "chimera.savedata=<file>" is the project's save data. */
	public static boolean start(String game, int w, int h, String[] settings)
	{
		// As the libretro front end: an exit request from the game does not
		// end the process.
		MobilePlatform.isLibretro = true;
		// Java's synthesizer, by default, renders through a "jitter
		// corrector": a thread that keeps time by the clock and sleeps. Here
		// the sound line already makes the synthesizer wait for each frame
		// (chimera.sound.Bus), and the two fought: music started 0.4 s late,
		// and after a soundfont was loaded it fell silent for a second
		// (chimera#301). The synthesizer reads this setting when it opens.
		java.util.prefs.Preferences.userRoot().node("/com/sun/media/sound/softsynthesizer").put("jitter correction", "false");
		width = w;
		height = h;
		Mobile.setPlatform(new MobilePlatform(w, h), new Runnable() { public void run() { settingsChanged(); } });
		Mobile.getPlatform().setPainter(new Runnable() { public void run() { pictures++; } });
		front = Mobile.getPlatform().getLcdFrontbuffer().getDataBuffer();
		lcd = new int[front.length];

		if (!Mobile.getPlatform().load(new File(game).toURI().toString()))
		{
			error = "error.could_not_load" + TEXT_ARG + game;
			return false;
		}
		if (Mobile.libretroRestartRequested != 0)
		{
			error = "error.text_encoding" + TEXT_ARG + Mobile.textEncoding + TEXT_ARG + System.getProperty("file.encoding");
			return false;
		}
		for (String s : settings)
		{
			final int eq = s.indexOf('=');
			if (eq <= 0) { continue; }
			final String name = s.substring(0, eq), value = s.substring(eq + 1);
			if (name.equals("chimera.savedata"))
			{
				if (!seedSaveData(value)) { return false; }
			}
			else if (name.startsWith("sys.")) { Mobile.config.sysSettings.put(name.substring(4), value); }
			else { Mobile.config.settings.put(name, value); }
		}
		Mobile.config.saveConfig();
		Mobile.config.saveSystemConfig();
		settingsChanged();
		// FreeJ2ME goes on with Java's own sounds when a soundfont does not
		// load; a project that asked for one is told instead (chimera#301)
		if (Mobile.useCustomMidi && Manager.getCustomSoundfont() == null)
		{
			error = "error.soundfont_unreadable";
			return false;
		}
		Mobile.getPlatform().runJar();
		return true;
	}

	/* keys: bit i held means FreeJ2ME's key i (Mobile.getMobileKey) is down.
	   touch: the screen is touched at (x, y), in pixels of the picture. */
	public static void frame(int keys, boolean touch, int x, int y)
	{
		final int picturesBefore = pictures;
		for (int i = 0; i < 20; i++)
		{
			final boolean now = ((keys >> i) & 1) != 0, was = ((held >> i) & 1) != 0;
			if (now && !was)
			{
				MobilePlatform.pressedKeys[i] = true;
				lastPressedKey = i;
				MobilePlatform.keyPressed(Mobile.getMobileKey(i));
			}
			else if (!now && was)
			{
				MobilePlatform.pressedKeys[i] = false;
				if (lastPressedKey == i) { lastPressedKey = pressedKey(); }
				MobilePlatform.keyReleased(Mobile.getMobileKey(i));
			}
		}
		held = keys;
		// a held key repeats once a frame, as on the libretro front end
		if (lastPressedKey != -1 && MobilePlatform.pressedKeys[lastPressedKey])
		{
			MobilePlatform.keyRepeated(Mobile.getMobileKey(lastPressedKey));
		}
		if (touch && !touching) { MobilePlatform.pointerPressed(x, y); }
		else if (touch && (x != touchX || y != touchY)) { MobilePlatform.pointerDragged(x, y); }
		else if (!touch && touching) { MobilePlatform.pointerReleased(touchX, touchY); }
		touching = touch;
		touchX = x;
		touchY = y;

		try { Thread.sleep(FRAME_NANOS / 1_000_000L, (int)(FRAME_NANOS % 1_000_000L)); }
		catch (InterruptedException e) { }
		newPicture = pictures != picturesBefore;

		synchronized (Mobile.getPlatform().getLcdFrontbuffer())
		{
			System.arraycopy(front, 0, lcd, 0, Math.min(front.length, lcd.length));
		}
		Bus.mix(audio, AUDIO_FRAMES);
	}

	/* The project's save data, put where FreeJ2ME keeps the game's record
	   stores (rms/<the game's suite name>/: a store is a description,
	   <name>.rms, and a file for each record) before the game runs: the .zip
	   Export Save Data writes. Save data that is not this game's is refused,
	   not ignored. */
	private static boolean seedSaveData(String file)
	{
		final String suite = Mobile.getPlatform().loader.suitename;
		final String dir = "rms/" + suite + "/";
		if (!file.toLowerCase().endsWith(".zip"))
		{
			error = "error.save_not_zip" + TEXT_ARG + file;
			return false;
		}
		try
		{
			int files = 0;
			ZipInputStream z = new ZipInputStream(new FileInputStream(file));
			try
			{
				for (ZipEntry e; (e = z.getNextEntry()) != null; )
				{
					if (e.isDirectory()) { continue; }
					final String name = e.getName();
					if (!name.startsWith("rms/") || name.contains("..") || name.contains("\\"))
					{
						error = "error.save_not_freej2me" + TEXT_ARG + name;
						return false;
					}
					if (!name.startsWith(dir) || name.length() == dir.length() || name.indexOf('/', dir.length()) >= 0)
					{
						error = "error.save_other_game" + TEXT_ARG + name + TEXT_ARG + dir;
						return false;
					}
					copy(z, new File(name));
					files++;
				}
			}
			finally { z.close(); }
			if (files == 0)
			{
				error = "error.save_empty";
				return false;
			}
			return true;
		}
		catch (IOException e)
		{
			error = "error.save_unreadable" + TEXT_ARG + e;
			return false;
		}
	}

	private static void copy(InputStream in, File to) throws IOException
	{
		to.getParentFile().mkdirs();
		FileOutputStream out = new FileOutputStream(to);
		try
		{
			final byte[] b = new byte[16384];
			for (int n; (n = in.read(b)) > 0; ) { out.write(b, 0, n); }
		}
		finally { out.close(); }
	}

	private static int pressedKey()
	{
		for (int i = 0; i < 20; i++)
		{
			if (MobilePlatform.pressedKeys[i]) { return i; }
		}
		return -1;
	}

	private static void settingsChanged()
	{
		Mobile.updateSettings();
		if (width != Mobile.lcdWidth || height != Mobile.lcdHeight)
		{
			width = Mobile.lcdWidth;
			height = Mobile.lcdHeight;
			Mobile.getPlatform().resizeLCD(width, height);
			front = Mobile.getPlatform().getLcdFrontbuffer().getDataBuffer();
			lcd = new int[front.length];
		}
	}
}
