#ifndef SF4CE_MOVIE_PLAYER_H
#define SF4CE_MOVIE_PLAYER_H
bool InitIntroMovie(bool enabled);
bool IntroMovieAvailable();
bool IntroMoviePlaying();
// Runs the original intro until EOF or touch / Cross / Circle / Start.
// NativeUpdate remains suspended, as it does behind Android's MovieActivity.
bool PlayIntroMovie(int width, int height, bool audio, bool (*debug_skip)());
#endif
