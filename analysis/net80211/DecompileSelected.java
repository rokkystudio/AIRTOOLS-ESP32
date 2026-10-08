// DecompileSelected.java
// @category AIRTOOLS
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class DecompileSelected extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] targets = {
            "esp_wifi_80211_tx",
            "ieee80211_raw_frame_sanity_check",
            "ieee80211_post_hmac_tx",
            "ieee80211_output_raw_process"
        };

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        println("PROGRAM=" + currentProgram.getName());
        println("LANGUAGE=" + currentProgram.getLanguageID());
        println("COMPILER=" + currentProgram.getCompilerSpec().getCompilerSpecID());

        for (String target : targets) {
            Function found = null;
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                Function function = functions.next();
                if (function.getName().equals(target)) {
                    found = function;
                    break;
                }
            }

            println("\n===== " + target + " =====");
            if (found == null) {
                println("NOT_FOUND");
                continue;
            }

            println("ENTRY=" + found.getEntryPoint());
            println("SIGNATURE=" + found.getSignature());

            DecompileResults result = decompiler.decompileFunction(found, 120, monitor);
            if (!result.decompileCompleted()) {
                println("DECOMPILE_FAILED=" + result.getErrorMessage());
                continue;
            }

            println(result.getDecompiledFunction().getC());
        }

        decompiler.dispose();
    }
}
