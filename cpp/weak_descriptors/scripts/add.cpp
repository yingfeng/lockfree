#include <iostream>
#include <cstdlib>

using namespace std;

int main(int argc, char** argv) {
	long long sum = 0;
	for (int i=1;i<argc;++i) {
		sum += atol(argv[i]);
	}
	cout<<sum;
	return 0;
}
