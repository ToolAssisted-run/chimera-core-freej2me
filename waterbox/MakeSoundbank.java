/* MakeSoundbank - writes the sound bank Gervill, the JDK's MIDI
 * synthesizer, would otherwise make the first time it is opened
 * (build-java.sh).
 *
 * The generator draws one set of random phases from Math.random(), which
 * nothing seeds, so two runs make two different banks. Math.random()'s
 * generator is seeded here first, so every build makes the same bank.
 * SPDX-License-Identifier: MIT */
import com.sun.media.sound.EmergencySoundbank;

public class MakeSoundbank
{
	public static void main(String[] args) throws Exception
	{
		Class<?> holder = Class.forName("java.lang.Math$RandomNumberGeneratorHolder");
		java.lang.reflect.Field f = holder.getDeclaredField("randomNumberGenerator");
		f.setAccessible(true);
		((java.util.Random)f.get(null)).setSeed(20261017L);
		EmergencySoundbank.createSoundbank().save(new java.io.File(args[0]));
	}
}
