/*
	The machine's sound, for the Chimera core of FreeJ2ME-Plus.
	GPL-3.0-or-later, like FreeJ2ME-Plus.

	Every line the program opens - a synthesizer's output, a sound effect's
	clip - plays into one stereo bus at 44100 samples a second. The front end
	takes 735 of them (1/60 s) at the end of every frame. Nothing here keeps
	time of its own: a line is played only when a frame takes the bus, and a
	writer whose line is full waits until a frame has taken some. So a
	synthesizer renders exactly as far ahead as its line holds, in every run.
*/
package chimera.sound;

import java.util.ArrayList;

public final class Bus
{
	public static final int RATE = 44100;

	/* guards every line's state; writers wait on it */
	static final Object lock = new Object();
	/* the lines that are open, in the order they were opened */
	static final ArrayList<BusLine> lines = new ArrayList<BusLine>();

	private static float[] left = new float[0], right = new float[0];

	private Bus() { }

	/* Plays every running line for the next 'frames' frames, into out as
	   interleaved 16-bit stereo. */
	public static void mix(short[] out, int frames)
	{
		if (left.length < frames)
		{
			left = new float[frames];
			right = new float[frames];
		}
		java.util.Arrays.fill(left, 0, frames, 0f);
		java.util.Arrays.fill(right, 0, frames, 0f);
		synchronized (lock)
		{
			for (int i = 0; i < lines.size(); i++)
			{
				lines.get(i).play(left, right, frames);
			}
			lock.notifyAll();
		}
		for (int i = 0; i < frames; i++)
		{
			out[2 * i] = clamp(left[i]);
			out[2 * i + 1] = clamp(right[i]);
		}
	}

	private static short clamp(float v)
	{
		int s = Math.round(v * 32767f);
		return (short)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
	}
}
