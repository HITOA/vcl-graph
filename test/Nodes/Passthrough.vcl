// Templated: the port type follows what's connected.
[AutoParameter]
using T = float32;

[Input("In")]
T input;

[Output("Out")]
T output;

[NodeProcess]
void Process() {
    output = input;
}
