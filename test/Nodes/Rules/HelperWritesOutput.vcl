// Rule 1: a helper names an output.
[Output("Out")]
float32 output = 0.0;

void Write() {
    output = 1.0;
}

[NodeProcess]
void Process() {
    Write();
}
