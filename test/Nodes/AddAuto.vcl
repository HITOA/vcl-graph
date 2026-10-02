// Both inputs follow what's connected, as Grog's Add does for scalars.
[AutoParameter]
using T = float32;

[Input("A")]
T a;
[Input("B")]
T b;

[Output("Out")]
T output;

[NodeProcess]
void Process() {
    output = a + b;
}
