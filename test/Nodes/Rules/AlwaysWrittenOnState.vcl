// [AlwaysWritten] only applies to an [Output].
[Output("Out")]
float32 output = 0.0;

[AlwaysWritten]
float32 state = 0.0;

[NodeProcess]
void Process() {
    state += 1.0;
    output = state;
}
