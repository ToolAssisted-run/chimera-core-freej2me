/*
	How Java Sound finds the machine's mixer: named in
	META-INF/services/javax.sound.sampled.spi.MixerProvider of the front
	end's jar. GPL-3.0-or-later, like FreeJ2ME-Plus.
*/
package chimera.sound;

import javax.sound.sampled.Mixer;
import javax.sound.sampled.spi.MixerProvider;

public final class ChimeraMixerProvider extends MixerProvider
{
	public Mixer.Info[] getMixerInfo() { return new Mixer.Info[] { ChimeraMixer.INFO }; }

	public Mixer getMixer(Mixer.Info info)
	{
		if (info == null || info.equals(ChimeraMixer.INFO)) { return ChimeraMixer.INSTANCE; }
		throw new IllegalArgumentException("no such mixer: " + info);
	}
}
