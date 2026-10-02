// An AutoParameter is a compile-time type.
[AutoParameter, Expose]
using T = float32;

[Output("Out")]
T output;

[NodeProcess]
void Process() {
    output = 1.0;
}
