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

	private static final long FRAME_NANOS = 16_666_667L;

	private static int[] front;
	private static int held, lastPressedKey = -1;
	private static boolean touching;
	private static int touchX, touchY;

	/* game: the file to load. settings: FreeJ2ME's own setting names and
	   values, "name=value", set before the game runs as the libretro front
	   end sets them; a name starting with "sys." is a system setting. */
	public static boolean start(String game, int w, int h, String[] settings)
	{
		// As the libretro front end: an exit request from the game does not
		// end the process.
		MobilePlatform.isLibretro = true;
		width = w;
		height = h;
		Mobile.setPlatform(new MobilePlatform(w, h), new Runnable() { public void run() { settingsChanged(); } });
		Mobile.getPlatform().setPainter(new Runnable() { public void run() { } });
		front = Mobile.getPlatform().getLcdFrontbuffer().getDataBuffer();
		lcd = new int[front.length];

		if (!Mobile.getPlatform().load(new File(game).toURI().toString()))
		{
			error = "FreeJ2ME could not load " + game;
			return false;
		}
		if (Mobile.libretroRestartRequested != 0)
		{
			error = "the game asks for the text encoding " + Mobile.textEncoding + " and the machine was started with "
				+ System.getProperty("file.encoding");
			return false;
		}
		for (String s : settings)
		{
			final int eq = s.indexOf('=');
			if (eq <= 0) { continue; }
			final String name = s.substring(0, eq), value = s.substring(eq + 1);
			if (name.startsWith("sys.")) { Mobile.config.sysSettings.put(name.substring(4), value); }
			else { Mobile.config.settings.put(name, value); }
		}
		Mobile.config.saveConfig();
		Mobile.config.saveSystemConfig();
		settingsChanged();
		Mobile.getPlatform().runJar();
		return true;
	}

	/* keys: bit i held means FreeJ2ME's key i (Mobile.getMobileKey) is down.
	   touch: the screen is touched at (x, y), in pixels of the picture. */
	public static void frame(int keys, boolean touch, int x, int y)
	{
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

		synchronized (Mobile.getPlatform().getLcdFrontbuffer())
		{
			System.arraycopy(front, 0, lcd, 0, Math.min(front.length, lcd.length));
		}
		Bus.mix(audio, AUDIO_FRAMES);
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
