# ProximityChatMod

This project is the native DLL that powers proximity voice chat for Elden Ring. It reads the game's memory to find the local player's position, HP, map area, camera facing, and current "block" ID, then streams that as plain text over a local TCP socket (127.0.0.1:9191) to the Electron app in elden-ring-prox-chat. That app uses the data to figure out which players are near each other in the game world and adjusts voice chat volume/muting between them accordingly.

It runs alongside the real Seamless Co-op multiplayer mod (ersc.dll) rather than replacing it - Seamless Co-op handles the actual multiplayer connection between players, this DLL only reads local game state for the proximity chat feature.

The dll scripting- clion folder contains the main dll work. This is done in c++ and involves finding functions and values in memory, then calling, hooking, reading, or writing to them. Most of the heavily technical/programming/reverse engineering work can be found here. The old visual studio folder for dllscripting is outdated, it merely exists as a code repository for anything I had before I rewrote signifigant parts of the project. 

The modengine section is the mod files for soulsmodding, they go in modengine2 or the like(from mapstudio/darkscript and whatnot). Don't bother trying to open these or use them unless you're famillar with making or using souls mods. Even still, don't bother. They're useless for now. 

The Cheat Engine section is used for fully functional proof of concept scripts for the dll mod. This helps with hammering out a quick proof of concept script(for things like respawn) to test before trying to create them in the dll variant. These might not work perfectly with cheat engine, they need the TGA cheat table to work. Even then, you may need to mess with the tga ce table a bit to get the script to run but all the pieces are included in this folder. 
