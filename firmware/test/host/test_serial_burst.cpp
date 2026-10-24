// A burst of serial input is answered one line per loop() pass, so the
// e-stop is sampled between lines (#39).
#include "host.h"
#include "globals.h"
#include <cstdio>
#include <string>

void setup();
void loop();

static int count(const std::string &s, const char *what)
{
    int n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1))
        n++;
    return n;
}

static void serial(const char *line)
{
    Serial.in += line;
    Serial.in += "\n";
    loop();
}

int main()
{
    setup();
    loop();
    serial("MODE=OPEN_LOOP");

    // Five lines arrive at once.
    Serial.out.clear();
    Serial.in = "R1TIME=10\nR1TIME=20\nR1TIME=30\nR1TIME=40\nR1TIME=50\n";
    loop();
    host_check("one pass answers one line of a five-line burst", count(Serial.out, "R1 time: ") == 1);
    host_check("the rest of the burst is still buffered", Serial.available() > 0);

    for (int i = 0; i < 4; i++)
        loop();
    host_check("four more passes answer the other four lines", count(Serial.out, "R1 time: ") == 5);
    host_check("the last line of the burst is applied", slice.relayHeater1.relayOnTime == 50);

    // The e-stop is sampled between the lines of a burst: a press during the
    // burst is asserted on the next pass, before the next line is handled,
    // and the lines after it are refused.
    Serial.out.clear();
    Serial.in = "R1TIME=60\nR1TIME=70\nR1TIME=80\n";
    loop();
    host_estop_level = LOW;
    loop();
    host_check("a press during a burst is asserted on the next pass", estopInput.asserted());
    host_check("the line after the press is refused, not applied",
               count(Serial.out, "R1 time: ") == 1 && count(Serial.out, "Ignored: e-stop held") == 1);

    return host_finish();
}
