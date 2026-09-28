// Stateful: counts calls. Reset starts it at 10.
[Output("Out")]
float32 output = 0.0;

float32 state = 0.0;

[NodeReset]
void Reset() {
    state = 10.0;
}

[NodeProcess]
void Process() {
    state += 1.0;
    output = state;
}
