/* multi.c -- main TU for the native multi-object / archive link demo.
   Calls say() from multi_helper.c (a separate object / archive member). */
void say(const char *s);

int main(void) {
  say("MULTI TU OK\r\n");
  return 0;
}
