# HMCL and JavaFX Hardware Acceleration

When the HMCL launcher reports "Software Rendering", add the Java flag `-Dprism.forceGPU=true` to enable GPU acceleration. The launcher UI uses JavaFX while Minecraft itself uses OpenGL, so the two operate independently.

Azul Zulu's JavaFX on Linux maintains a driver allowlist that does not include `freedreno`. Even when Mesa already provides Adreno hardware acceleration, JavaFX logs `Failed Graphics Hardware Qualifier check` and falls back to `SWPipeline`. This flag bypasses the allowlist check.

When running the JAR directly:

```sh
java -Dprism.forceGPU=true -jar HMCL.jar
```

When using HMCL's `.sh` launcher bundle:

```sh
HMCL_JAVA_OPTS="-Dprism.forceGPU=true" bash HMCL.sh
```

Your Java runtime must include JavaFX for ARM64. Do not set `JAVA_TOOL_OPTIONS` globally, and do not spoof the GPU vendor name. This flag does not force Minecraft onto a specific driver; the game still initializes through its own LWJGL / OpenGL stack.

## Verifying It Works

Temporarily add `-Dprism.verbose=true` to print which rendering pipeline is active. On HMCL 3.17.0.359 with Zulu Java 25.0.4.1, the pipeline changes from `SWPipeline` before adding the flag to `ES2Pipeline` afterward, while `glxinfo -B` reports Adreno 830v1 and `Accelerated: yes`. This confirms the launcher uses GPU rendering and does not reflect in-game frame rates.

The corresponding JavaFX implementation is in [X11GLFactory.java](https://github.com/openjdk/jfx/blob/master/modules/javafx.graphics/src/main/java/com/sun/prism/es2/X11GLFactory.java).
