using System;
using System.Runtime.InteropServices;


namespace Example
{
  internal class Program
  {
    #region TYPES_PRIVATE
    private enum AUDCLNT_SHAREMODE : UInt32
    {
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_SHAREMODE_EXCLUSIVE,
    }
    #endregion TYPES_PRIVATE

    #region CONSTANTS_PRIVATE
    private static UInt16 CHANNNELS
    {
      get;
    } = 2;

    private static UInt16 BITS_PER_SAMPLE
    {
      get;
    } = 16;

    private static UInt32 SAMPLES_PER_SEC
    {
      get;
    } = 44_100;
    #endregion CONSTANTS_PRIVATE

    #region STATIC_METHODS_WASAPI_STREAMER
    [DllImport("WASAPI_Streamer", EntryPoint ="Initialize")]
    private static extern UInt32 WASAPIStreamer_Initialize(
      AUDCLNT_SHAREMODE mode, 
      UInt16 nChannels,
      UInt16 nBitsPerSample,
      UInt32 nSamplesPerSec
    );

    // NOTE: REQUIRED CALL, AT END OF PROGRAM.
    [DllImport("WASAPI_Streamer", EntryPoint = "Uninitialize")]
    private static extern void WASAPIStreamer_Uninitialize();

    [DllImport("WASAPI_Streamer", EntryPoint = "Start")]
    private static extern void WASAPIStreamer_Start(Action ehMemoryReleased);

    [DllImport("WASAPI_Streamer", EntryPoint = "Stop")]
    private static extern void WASAPIStreamer_Stop();

    [DllImport("WASAPI_Streamer", EntryPoint = "GetIsStreaming")]
    private static extern bool WASAPIStreamer_GetIsStreaming();

    [DllImport("WASAPI_Streamer", EntryPoint = "GetBufferSize")]
    private static extern UInt32 WASAPIStreamer_GetBufferSize();

    [DllImport("WASAPI_Streamer", EntryPoint = "CopyToBuffer")]
    private static extern bool WASAPIStreamer_CopyToBuffer(byte[] data, UInt32 length);
    #endregion STATIC_METHODS_WASAPI_STREAMER

    #region STATIC_METHODS
    public static void Main(string[] args)
    {
      // IntPtr ptr = System.Runtime.InteropServices.Marshal.GetFunctionPointerForDelegate(objDelegate);
      // GCHandle gch = GCHandle.Alloc(objDelegate);

      // Initialize WASAPI_Streamer.
      Program.WASAPIStreamer_Initialize(
        mode: AUDCLNT_SHAREMODE.AUDCLNT_SHAREMODE_EXCLUSIVE, 
        nChannels: Program.CHANNNELS,
        nBitsPerSample: Program.BITS_PER_SAMPLE,
        nSamplesPerSec: Program.SAMPLES_PER_SEC
      );

      // Start streming.
      {
        const double PI_2 = Math.PI * 2;
        const uint FREQ_L = 4000;
        const uint FREQ_R = 2000;
        const uint VOLUME_L = 6000;
        const uint VOLUME_R = 4000;
        const double TEMP_L = (PI_2 * FREQ_L);
        const double TEMP_R = (PI_2 * FREQ_R);
        double SAMPLES_PER_SEC_INV = (double)1 / Program.SAMPLES_PER_SEC;
        double time = 0;

        // NOTE: BE CAREFUL ABOUT LIFETIME OF FUNCTION OBJECT.
        Program.WASAPIStreamer_Start(() => {
          UInt16[] data = new UInt16[(UInt32)(Program.WASAPIStreamer_GetBufferSize() / (Program.BITS_PER_SAMPLE / 8))];

          for (int idx = 0; idx < data.Length; idx += 2) {
            data[idx + 0] = (UInt16)(Math.Sin(time * TEMP_L) * VOLUME_L);
            data[idx + 1] = (UInt16)(Math.Sin(time * TEMP_R) * VOLUME_R);

            time += SAMPLES_PER_SEC_INV;
          }

          // Copy to buffer.
          {
            byte[] dataByte = MemoryMarshal.AsBytes(data.AsSpan()).ToArray();

            Program.WASAPIStreamer_CopyToBuffer(dataByte, (UInt32)dataByte.Length);
          }
        });
      }

      //
      while (Console.ReadKey(true).Key != ConsoleKey.Spacebar) {
        continue;
      }

      // Stop streaming.
      Program.WASAPIStreamer_Stop();

      // Uninitialize WASAPI_Streamer.
      Program.WASAPIStreamer_Uninitialize();

      // Release delegate object.
      // gch.Free();

      return;
    }
    #endregion STATIC_METHODS
  }
}
