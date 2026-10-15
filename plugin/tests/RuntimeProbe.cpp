#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
	if (argc != 2) { return 1; }
	const auto& module = REL::Module::get();
	const auto version = module.version();
	const bool ae = std::string_view{ argv[1] } == "AE";
	if (REL::Module::IsAE() != ae || REL::Module::IsVR()) { return 2; }
	alignas(16) std::byte vmStorage[0x9000]{};
	auto* vm = reinterpret_cast<RE::SkyrimVM*>(vmStorage);
	const auto delta = reinterpret_cast<std::byte*>(&vm->GetVMRuntimeData()) - vmStorage;
	if (delta != (version >= REL::Version{ 1, 7, 99, 0 } ? 0x210 : 0x200)) { return 3; }
	if (version >= REL::Version{ 1, 7, 99, 0 }) {
		// Synthetic format-5 file, only in the isolated CTest working directory.
		// This exercises the production filename selection + loader + dense lookup.
		std::filesystem::create_directories("Data/SKSE/Plugins");
		const auto path = "Data/SKSE/Plugins/versionlib-" + version.string() + ".bin";
		std::ofstream file{ path, std::ios::binary };
		const auto integer = [&](std::uint32_t n) { file.write(reinterpret_cast<const char*>(&n), 4); };
		integer(5);
		for (int i = 0; i < 4; ++i) { integer(version[i]); }
		const char name[64] = "ScavengerSense synthetic test fixture";
		file.write(name, sizeof(name));
		integer(8); integer(0); integer(3);
		integer(0); integer(0x1000); integer(0x2000);
		file.close();
		if (REL::ID{ 1 }.offset() != 0x1000 || REL::ID{ 2 }.offset() != 0x2000) { return 4; }
	}
	std::cout << version.string() << ": " << (ae ? "AE" : "SE") << ", VM +0x" << std::hex << delta << " passed\n";
	return 0;
}
