/*
	BMX sound glue for Mini vMac (Macintosh 128K).

	Shadows the setup-tool generated SOUNDGLU.h (which includes the
	ALSA backend SGLUALSA.h). BMX drains 8-bit 22254Hz mono into the
	Circle sink directly in src/mac128k/mac128k_bmx_audio.cpp, so no
	host sound header is needed here.
*/
