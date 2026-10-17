/*
	A SourceDataLine: the program writes, the bus plays what was written.
	GPL-3.0-or-later, like FreeJ2ME-Plus.

	What is written waits in the line's buffer until a frame plays it. A
	write that does not fit waits too, until a frame has made room - this is
	what paces a synthesizer's thread. The buffer is held to three frames
	(1/20 s) whatever the program asks, so sound does not trail the picture
	by more than that.
*/
package chimera.sound;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.Line;
import javax.sound.sampled.LineUnavailableException;
import javax.sound.sampled.SourceDataLine;

final class SourceLine extends BusLine implements SourceDataLine
{
	private static final int MAX_FRAMES = 3 * Bus.RATE / 60;

	private byte[] buf = new byte[0];
	private int head, count;      /* the oldest byte not played, and how many wait */
	private long played;          /* the line's frames played since it opened */
	private int frameSize;

	SourceLine(Line.Info info, AudioFormat format) { super(info, format); }

	public void open(AudioFormat f) throws LineUnavailableException { open(f, MAX_FRAMES * f.getFrameSize()); }

	public void open(AudioFormat f, int bufferSize) throws LineUnavailableException
	{
		if (!playable(f) || f.getFrameSize() <= 0) { throw new IllegalArgumentException("not a format this line plays: " + f); }
		synchronized (Bus.lock)
		{
			if (open) { return; }
			format = f;
			frameSize = f.getFrameSize();
			int frames = bufferSize / frameSize;
			final int most = (int)Math.ceil(MAX_FRAMES * f.getFrameRate() / Bus.RATE);
			if (frames > most || frames <= 0) { frames = most; }
			buf = new byte[frames * frameSize];
			head = count = 0;
			played = 0;
		}
		join();
	}

	public int write(byte[] b, int off, int len)
	{
		if (len % frameSize != 0) { throw new IllegalArgumentException("not a whole number of frames: " + len); }
		int done = 0;
		synchronized (Bus.lock)
		{
			while (done < len && open)
			{
				if (count == buf.length)
				{
					// a stopped line keeps what it holds, and a write waits for start
					try { Bus.lock.wait(); }
					catch (InterruptedException e) { break; }
					continue;
				}
				final int tail = (head + count) % buf.length;
				final int n = Math.min(len - done, Math.min(buf.length - count, buf.length - tail));
				System.arraycopy(b, off + done, buf, tail, n);
				count += n;
				done += n;
			}
		}
		return done;
	}

	void play(float[] left, float[] right, int frames)
	{
		if (!running) { return; }
		final float[] v = volume();
		final double step = step();
		for (int i = 0; i < frames; i++)
		{
			// the line's frame at 'phase' and the one after it, while there are
			if (count >= frameSize)
			{
				final float l0 = sample(buf, head, 0), r0 = sample(buf, head, 1);
				float l = l0, r = r0;
				if (count >= 2 * frameSize && phase > 0)
				{
					final int next = (head + frameSize) % buf.length;
					l += (float)((sample(buf, next, 0) - l0) * phase);
					r += (float)((sample(buf, next, 1) - r0) * phase);
				}
				left[i] += l * v[0];
				right[i] += r * v[1];
			}
			phase += step;
			while (phase >= 1.0 && count >= frameSize)
			{
				phase -= 1.0;
				head = (head + frameSize) % buf.length;
				count -= frameSize;
				played++;
			}
			if (count < frameSize) { phase = 0; }
		}
	}

	public void drain()
	{
		synchronized (Bus.lock)
		{
			while (open && running && count > 0)
			{
				try { Bus.lock.wait(); }
				catch (InterruptedException e) { return; }
			}
		}
	}

	public void flush()
	{
		synchronized (Bus.lock)
		{
			head = count = 0;
			phase = 0;
			Bus.lock.notifyAll();
		}
	}

	public int available() { synchronized (Bus.lock) { return buf.length - count; } }
	public int getBufferSize() { synchronized (Bus.lock) { return buf.length; } }
	public boolean isActive() { synchronized (Bus.lock) { return running && count > 0; } }
	public long getLongFramePosition() { synchronized (Bus.lock) { return played; } }

	public void close()
	{
		super.close();
		synchronized (Bus.lock) { head = count = 0; }
	}
}
