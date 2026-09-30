@import "Block.vcl";

// Reads the first channel of the block it's given: valid as declared, but fails to compile when
// its input is inferred to be a scalar.
[AutoParameter]
using InputType = Block::Block<float32, 1>;

[Input("In")]
InputType input;

[Output("Out")]
float32 output = 0.0;

[NodeProcess]
void Process() {
    output = input.channels[0];
}
