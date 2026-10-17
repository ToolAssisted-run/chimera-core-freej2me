/*
	A Clip: a sound held whole, played from a position, looped if asked.
	GPL-3.0-or-later, like FreeJ2ME-Plus.

	When a clip plays to its end it stops by itself and says so (a STOP
	event), as Java Sound's clips do; FreeJ2ME's players loop and end on it.
*/
package chimera.sound;

import java.io.ByteArrayOutputStream;
import java.io.IOException;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.Clip;
import javax.sound.sampled.Line;
import javax.sound.sampled.LineEvent;
import javax.sound.sampled.LineUnavailableException;

final class ClipLine extends BusLine implements Clip
{
	private byte[] data = new byte[0];
	private int frames, frameSize;
	private int pos;                 /* the next frame to play */
	private int loopStart, loopEnd;  /* loopEnd is the last frame of the loop */
	private int loopsLeft;           /* LOOP_CONTINUOUSLY, or how many more times */

	ClipLine(Line.Info info, AudioFormat format) { super(info, format); }

	public void open(AudioFormat f, byte[] d, int offset, int len) throws LineUnavailableException
	{
		if (!playable(f) || f.getFrameSize() <= 0) { throw new IllegalArgumentException("not a format this clip plays: " + f); }
		synchronized (Bus.lock)
		{
			if (open) { throw new IllegalStateException("the clip is already open"); }
			format = f;
			frameSize = f.getFrameSize();
			frames = len / frameSize;
			data = new byte[frames * frameSize];
			System.arraycopy(d, offset, data, 0, data.length);
			pos = 0;
			loopStart = 0;
			loopEnd = frames - 1;
			loopsLeft = 0;
		}
		join();
	}

	public void open(AudioInputStream stream) throws LineUnavailableException, IOException
	{
		AudioFormat f = stream.getFormat();
		if (!playable(f))
		{
			// u-law, a-law and the like: as 16-bit samples
			final AudioFormat pcm = new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, f.getSampleRate(), 16,
				f.getChannels(), 2 * f.getChannels(), f.getSampleRate(), false);
			stream = AudioSystem.getAudioInputStream(pcm, stream);
			f = pcm;
		}
		final ByteArrayOutputStream all = new ByteArrayOutputStream();
		final byte[] chunk = new byte[16384];
		for (int n; (n = stream.read(chunk)) > 0; ) { all.write(chunk, 0, n); }
		final byte[] d = all.toByteArray();
		open(f, d, 0, d.length);
	}

	void play(float[] left, float[] right, int count)
	{
		if (!running) { return; }
		final float[] v = volume();
		final double step = step();
		boolean ended = false;
		for (int i = 0; i < count; i++)
		{
			if (pos >= frames) { ended = true; break; }
			final int at = pos * frameSize;
			float l = sample(data, at, 0), r = sample(data, at, 1);
			if (phase > 0 && pos + 1 < frames)
			{
				l += (float)((sample(data, at + frameSize, 0) - l) * phase);
				r += (float)((sample(data, at + frameSize, 1) - r) * phase);
			}
			left[i] += l * v[0];
			right[i] += r * v[1];
			phase += step;
			while (phase >= 1.0)
			{
				phase -= 1.0;
				if (pos == loopEnd && loopsLeft != 0)
				{
					pos = loopStart;
					if (loopsLeft > 0) { loopsLeft--; }
				}
				else { pos++; }
			}
		}
		if (ended || pos >= frames)
		{
			running = false;
			event(LineEvent.Type.STOP, pos);
		}
	}

	public void loop(int count)
	{
		synchronized (Bus.lock)
		{
			loopsLeft = count;
			if (pos > loopEnd) { pos = loopStart; }
		}
		start();
	}

	public void setLoopPoints(int start, int end)
	{
		synchronized (Bus.lock)
		{
			if (start < 0 || start >= frames || (end != -1 && (end < start || end >= frames)))
			{
				throw new IllegalArgumentException("loop points outside the clip");
			}
			loopStart = start;
			loopEnd = end == -1 ? frames - 1 : end;
		}
	}

	public void setFramePosition(int frame)
	{
		synchronized (Bus.lock)
		{
			pos = frame < 0 ? 0 : frame > frames ? frames : frame;
			phase = 0;
		}
	}

	public void setMicrosecondPosition(long us) { setFramePosition((int)(us * format.getFrameRate() / 1000000.0)); }
	public int getFrameLength() { synchronized (Bus.lock) { return frames; } }
	public long getMicrosecondLength() { return (long)(getFrameLength() * 1000000.0 / format.getFrameRate()); }
	public long getLongFramePosition() { synchronized (Bus.lock) { return pos; } }
	public int getBufferSize() { synchronized (Bus.lock) { return data.length; } }
	public int available() { return 0; }
	public boolean isActive() { return isRunning(); }
	public void drain()
	{
		synchronized (Bus.lock)
		{
			while (open && running)
			{
				try { Bus.lock.wait(); }
				catch (InterruptedException e) { return; }
			}
		}
	}
	public void flush() { }
}
