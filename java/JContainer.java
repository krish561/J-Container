import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class JContainer {

    public static void main(String[] args) {
        System.out.println("[Java] JContainer Orchestrator v1.0");

        if (args.length < 2 || !args[0].equals("run")) {
            System.err.println("Usage: java JContainer run <rootfs_path> <command>");
            System.err.println("Example: java JContainer run ../rootfs /bin/sh");
            System.exit(1);
        }

        // 1. Locate the Native Binary (../container-shim)
        File binary = new File("../container-shim");
        if (!binary.exists()) {
            System.err.println("Error: Native binary not found at: " + binary.getAbsolutePath());
            System.err.println("Did you compile it? (gcc -o container-shim container.c)");
            System.exit(1);
        }

        // 2. Locate the RootFS
        File rootfs = new File(args[1]);
        if (!rootfs.exists()) {
            System.err.println("Error: RootFS not found at: " + rootfs.getAbsolutePath());
            System.exit(1);
        }

        // 3. Build the Command
        // We need: [absolute_path_to_shim, "run", absolute_path_to_rootfs, command...]
        List<String> command = new ArrayList<>();
        command.add(binary.getAbsolutePath());
        command.add("run");
        command.add(rootfs.getAbsolutePath());
        
        // Add the rest of the arguments (the command to run inside container)
        // args[0] is "run", args[1] is rootfs. So we start from args[2].
        for (int i = 2; i < args.length; i++) {
            command.add(args[i]);
        }

        System.out.println("[Java] Handing over control to native shim...");
        System.out.println("[Java] Command: " + command);
        System.out.println("------------------------------------------------");

        try {
            ProcessBuilder pb = new ProcessBuilder(command);
            
            // CRITICAL: Connect the Container's terminal to Java's terminal
            pb.inheritIO();

            Process process = pb.start();
            int exitCode = process.waitFor();

            System.out.println("------------------------------------------------");
            System.out.println("[Java] Container session ended. Exit Code: " + exitCode);

        } catch (IOException | InterruptedException e) {
            e.printStackTrace();
        }
    }
}
