// Warning: the initializer of an [AlwaysWritten] output is never observed.
[Output("Out"), AlwaysWritten]
float32 output = 5.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
