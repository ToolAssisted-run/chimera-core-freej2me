/*
	The one mixer the machine has: it makes the bus's lines.
	GPL-3.0-or-later, like FreeJ2ME-Plus.

	The core links no sound device of the JDK's (no libjsound), so this is
	the only mixer Java Sound finds: AudioSystem.getClip() and
	getSourceDataLine() come here, and so does Gervill, the JDK's MIDI
	synthesizer, when it opens its output.
*/
package chimera.sound;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.Clip;
import javax.sound.sampled.Control;
import javax.sound.sampled.DataLine;
import javax.sound.sampled.Line;
import javax.sound.sampled.LineListener;
import javax.sound.sampled.LineUnavailableException;
import javax.sound.sampled.Mixer;
import javax.sound.sampled.SourceDataLine;

public final class ChimeraMixer implements Mixer
{
	static final ChimeraMixer INSTANCE = new ChimeraMixer();
	static final Mixer.Info INFO = new Mixer.Info("Chimera", "Chimera", "The machine's sound", "1") { };

	/* a line asked for with no format plays the bus's own */
	static final AudioFormat DEFAULT = new AudioFormat(Bus.RATE, 16, 2, true, false);

	private static final AudioFormat[] FORMATS = {
		new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED,
			AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, false),
		new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED,
			AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, true),
		new AudioFormat(AudioFormat.Encoding.PCM_UNSIGNED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED,
			AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, false),
		new AudioFormat(AudioFormat.Encoding.PCM_FLOAT, AudioSystem.NOT_SPECIFIED, 32,
			AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED, false),
	};
	private static final Line.Info[] SOURCES = {
		new DataLine.Info(SourceDataLine.class, FORMATS, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED),
		new DataLine.Info(Clip.class, FORMATS, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED),
	};

	private ChimeraMixer() { }

	private static AudioFormat asked(Line.Info info)
	{
		if (info instanceof DataLine.Info)
		{
			for (AudioFormat f : ((DataLine.Info)info).getFormats())
			{
				if (f.getSampleRate() != AudioSystem.NOT_SPECIFIED && f.getChannels() != AudioSystem.NOT_SPECIFIED
					&& f.getSampleSizeInBits() != AudioSystem.NOT_SPECIFIED && f.getFrameSize() != AudioSystem.NOT_SPECIFIED)
				{
					return f;
				}
			}
		}
		return DEFAULT;
	}

	/* as Java Sound's own mixers ask it: does what is asked for match a
	   line this mixer offers (the asked format among the offered ones) */
	public boolean isLineSupported(Line.Info info)
	{
		for (Line.Info s : SOURCES)
		{
			if (info.matches(s)) { return true; }
		}
		return false;
	}

	public Line getLine(Line.Info info) throws LineUnavailableException
	{
		if (!isLineSupported(info)) { throw new IllegalArgumentException("no such line: " + info); }
		final Class<?> c = info.getLineClass();
		if (c.isAssignableFrom(SourceLine.class)) { return new SourceLine(SOURCES[0], asked(info)); }
		return new ClipLine(SOURCES[1], asked(info));
	}

	public Mixer.Info getMixerInfo() { return INFO; }
	public Line.Info[] getSourceLineInfo() { return SOURCES.clone(); }
	public Line.Info[] getTargetLineInfo() { return new Line.Info[0]; }
	public Line.Info[] getSourceLineInfo(Line.Info info) { return isLineSupported(info) ? new Line.Info[] { info } : new Line.Info[0]; }
	public Line.Info[] getTargetLineInfo(Line.Info info) { return new Line.Info[0]; }
	public int getMaxLines(Line.Info info) { return isLineSupported(info) ? AudioSystem.NOT_SPECIFIED : 0; }
	public Line[] getSourceLines() { synchronized (Bus.lock) { return Bus.lines.toArray(new Line[0]); } }
	public Line[] getTargetLines() { return new Line[0]; }
	public void synchronize(Line[] lines, boolean maintainSync) { throw new IllegalArgumentException("lines are not synchronized"); }
	public void unsynchronize(Line[] lines) { }
	public boolean isSynchronizationSupported(Line[] lines, boolean maintainSync) { return false; }

	/* Line: the mixer itself is always open */
	public Line.Info getLineInfo() { return new Line.Info(Mixer.class); }
	public void open() { }
	public void close() { }
	public boolean isOpen() { return true; }
	public Control[] getControls() { return new Control[0]; }
	public boolean isControlSupported(Control.Type t) { return false; }
	public Control getControl(Control.Type t) { throw new IllegalArgumentException("no " + t + " control"); }
	public void addLineListener(LineListener l) { }
	public void removeLineListener(LineListener l) { }
}
