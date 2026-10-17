/*
	A line that plays into the bus: what a SourceDataLine and a Clip have in
	common. GPL-3.0-or-later, like FreeJ2ME-Plus.

	The line's own format (rate, sample size, channels) is turned into the
	bus's 44100 Hz stereo as it plays: a mono line plays on both sides, a
	line of another rate is stepped through at its own rate with the samples
	in between drawn as straight lines.
*/
package chimera.sound;

import java.util.ArrayList;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.BooleanControl;
import javax.sound.sampled.Control;
import javax.sound.sampled.DataLine;
import javax.sound.sampled.FloatControl;
import javax.sound.sampled.Line;
import javax.sound.sampled.LineEvent;
import javax.sound.sampled.LineListener;

abstract class BusLine implements DataLine
{
	final Line.Info info;
	AudioFormat format;
	boolean open, running;
	private final ArrayList<LineListener> listeners = new ArrayList<LineListener>();

	final FloatControl gain = new FloatControl(FloatControl.Type.MASTER_GAIN, -80f, 6.0206f, 0.01f, -1, 0f, "dB") { };
	final BooleanControl mute = new BooleanControl(BooleanControl.Type.MUTE, false) { };
	final FloatControl pan = new FloatControl(FloatControl.Type.PAN, -1f, 1f, 0.01f, -1, 0f, "") { };

	/* where the line is between two of its frames, while it plays at another rate */
	double phase;

	BusLine(Line.Info info, AudioFormat format)
	{
		this.info = info;
		this.format = format;
	}

	/* adds the next 'frames' frames of this line to the bus; called with Bus.lock held */
	abstract void play(float[] left, float[] right, int frames);

	/* ---- the line's samples ---- */

	static boolean playable(AudioFormat f)
	{
		final AudioFormat.Encoding e = f.getEncoding();
		final int bits = f.getSampleSizeInBits();
		if (e.equals(AudioFormat.Encoding.PCM_FLOAT)) { return bits == 32 || bits == AudioSystem.NOT_SPECIFIED; }
		return (e.equals(AudioFormat.Encoding.PCM_SIGNED) || e.equals(AudioFormat.Encoding.PCM_UNSIGNED))
			&& (bits == 8 || bits == 16 || bits == 24 || bits == 32 || bits == AudioSystem.NOT_SPECIFIED);
	}

	/* channel ch (0 or 1) of the frame starting at byte 'at', from -1 to 1 */
	final float sample(byte[] data, int at, int ch)
	{
		final int channels = format.getChannels();
		final int bytes = format.getSampleSizeInBits() / 8;
		final int p = at + (ch < channels ? ch : channels - 1) * bytes;
		final boolean big = format.isBigEndian();
		final AudioFormat.Encoding e = format.getEncoding();
		if (bytes == 1)
		{
			final int v = data[p];
			return e.equals(AudioFormat.Encoding.PCM_UNSIGNED) ? ((v & 0xff) - 128) / 128f : v / 128f;
		}
		int v = 0;
		for (int i = 0; i < bytes; i++)
		{
			final int b = data[p + (big ? i : bytes - 1 - i)] & 0xff;
			v = (v << 8) | b;
		}
		if (e.equals(AudioFormat.Encoding.PCM_FLOAT)) { return Float.intBitsToFloat(v); }
		final int shift = 32 - 8 * bytes;
		v = (v << shift) >> shift; // sign-extend
		if (e.equals(AudioFormat.Encoding.PCM_UNSIGNED)) { v -= 1 << (8 * bytes - 1); }
		return v / (float)(1L << (8 * bytes - 1));
	}

	/* the gain to play at, for the left and right side */
	final float[] volume()
	{
		if (mute.getValue()) { return new float[] { 0f, 0f }; }
		final float g = gain.getValue() <= -80f ? 0f : (float)StrictMath.pow(10.0, gain.getValue() / 20.0);
		final float p = pan.getValue();
		return new float[] { g * (p > 0 ? 1f - p : 1f), g * (p < 0 ? 1f + p : 1f) };
	}

	/* how many of the line's own frames go by in one frame of the bus */
	final double step() { return format.getFrameRate() / Bus.RATE; }

	/* ---- events, delivered on a thread of their own, as Java Sound does ---- */

	final void event(LineEvent.Type type, long position)
	{
		final LineListener[] to;
		synchronized (listeners) { to = listeners.toArray(new LineListener[0]); }
		if (to.length > 0) { Events.post(to, new LineEvent(this, type, position)); }
	}

	public void addLineListener(LineListener l) { synchronized (listeners) { listeners.add(l); } }
	public void removeLineListener(LineListener l) { synchronized (listeners) { listeners.remove(l); } }

	/* ---- Line and DataLine ---- */

	public Line.Info getLineInfo() { return info; }
	public AudioFormat getFormat() { return format; }
	public boolean isOpen() { synchronized (Bus.lock) { return open; } }
	public boolean isRunning() { synchronized (Bus.lock) { return running; } }
	public void open() throws javax.sound.sampled.LineUnavailableException { throw new IllegalArgumentException("a data line opens with a format"); }
	public float getLevel() { return AudioSystem.NOT_SPECIFIED; }
	public long getMicrosecondPosition() { return (long)(getLongFramePosition() * 1000000.0 / format.getFrameRate()); }
	public int getFramePosition() { return (int)getLongFramePosition(); }

	public Control[] getControls() { return new Control[] { gain, mute, pan }; }
	public boolean isControlSupported(Control.Type t) { return t == gain.getType() || t == mute.getType() || t == pan.getType(); }
	public Control getControl(Control.Type t)
	{
		if (t == gain.getType()) { return gain; }
		if (t == mute.getType()) { return mute; }
		if (t == pan.getType()) { return pan; }
		throw new IllegalArgumentException("no " + t + " control");
	}

	/* opens the line: it joins the bus */
	final void join()
	{
		synchronized (Bus.lock)
		{
			open = true;
			running = false;
			phase = 0;
			if (!Bus.lines.contains(this)) { Bus.lines.add(this); }
		}
		event(LineEvent.Type.OPEN, 0);
	}

	public void close()
	{
		synchronized (Bus.lock)
		{
			if (!open) { return; }
			open = false;
			running = false;
			Bus.lines.remove(this);
			Bus.lock.notifyAll();
		}
		event(LineEvent.Type.CLOSE, getLongFramePosition());
	}

	public void start()
	{
		synchronized (Bus.lock)
		{
			if (!open || running) { return; }
			running = true;
		}
		event(LineEvent.Type.START, getLongFramePosition());
	}

	public void stop()
	{
		synchronized (Bus.lock)
		{
			if (!running) { return; }
			running = false;
			Bus.lock.notifyAll();
		}
		event(LineEvent.Type.STOP, getLongFramePosition());
	}
}
