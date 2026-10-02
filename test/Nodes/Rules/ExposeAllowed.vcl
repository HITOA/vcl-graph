// Every allowed spelling, on every kind of variable that may be exposed.
[Input("In"), Expose(Write)]
float32 input = 0.0;

[Input("Gain"), Expose(Read, Write)]
float32 gain = 1.0;

[Input("Bias"), Expose(Read)]
float32 bias = 0.0;

[Output("Out"), Expose, AlwaysWritten]
float32 output;

[Output("Peak"), Expose(Read)]
float32 peak = 0.0;

[Expose]
uint32 calls = 0;

[Expose(Write)]
float32 offset = 0.0;

float32 hidden = 0.0;

[NodeProcess]
void Process() {
    calls += 1;
    hidden = input * gain + bias + offset;
    output = hidden;
    if (hidden > peak)
        peak = hidden;
}
