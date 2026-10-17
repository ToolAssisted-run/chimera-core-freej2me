/*
	Line events (open, start, stop, close), delivered one at a time on a
	thread of their own, as Java Sound's own lines deliver them: a listener
	never runs on the thread that played the sound or wrote to the line.
	GPL-3.0-or-later, like FreeJ2ME-Plus.
*/
package chimera.sound;

import java.util.ArrayDeque;

import javax.sound.sampled.LineEvent;
import javax.sound.sampled.LineListener;

final class Events implements Runnable
{
	private static final ArrayDeque<Object[]> queue = new ArrayDeque<Object[]>();
	private static Thread thread;

	private Events() { }

	static void post(LineListener[] to, LineEvent e)
	{
		synchronized (queue)
		{
			queue.add(new Object[] { to, e });
			if (thread == null)
			{
				thread = new Thread(new Events(), "Chimera sound events");
				thread.setDaemon(true);
				thread.start();
			}
			queue.notifyAll();
		}
	}

	public void run()
	{
		for (;;)
		{
			Object[] next;
			synchronized (queue)
			{
				while (queue.isEmpty())
				{
					try { queue.wait(); }
					catch (InterruptedException e) { }
				}
				next = queue.poll();
			}
			for (LineListener l : (LineListener[])next[0])
			{
				try { l.update((LineEvent)next[1]); }
				catch (RuntimeException e) { e.printStackTrace(); }
			}
		}
	}
}
