using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace RejitTest
{
    internal class Program
    {
        static void Main(string[] args)
        {
            var build = typeof(Program).Assembly.GetCustomAttribute<AssemblyConfigurationAttribute>()?.Configuration ?? "Unknown";
            var arch = Environment.Is64BitProcess ? "x64" : "x86";
            Console.WriteLine($"Running {build} build as {arch} on {RuntimeInformation.FrameworkDescription}");
            Console.WriteLine();
            var counter = new Counter();
            var test = (ITestCase)Activator.CreateInstance("RejitTest", "RejitTest.TestCase").Unwrap();
            test.Run(); // comment out this line to test without relying on rejit support
            Console.WriteLine($"Counter: Test method called {counter.Read()} times - expecting 0.");
            Console.WriteLine();
            Console.WriteLine("Installing callback.");
            Runtime.InstallCounter(typeof(TestSubject).GetMethod(nameof(TestSubject.TestMethod)), counter);
            Console.WriteLine();
            test.Run();
            Console.WriteLine($"Counter: Test method called {counter.Read()} times - expecting 1.");
            Console.WriteLine();
            Console.WriteLine("Press any key to exit.");
            Console.ReadKey(true);
        }
    }

    internal interface ITestCase
    {
        void Run();
    }

    internal class TestCase : ITestCase
    {
        void ITestCase.Run()
        {
            TestSubject.TestMethod();
        }
    }

    internal class TestSubject
    {
        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void TestMethod()
        {
            Console.WriteLine("Subject: Test method called.");
        }
    }

    internal sealed unsafe class Counter
    {
        // leaking the counters in this repro for simplicity
        private int* mCounter = (int*)Marshal.AllocCoTaskMem(sizeof(int));
        public Counter() { *mCounter = 0; }
        public long GetAddress() => (nint)mCounter;
        public int Read() => *mCounter;
    }

    internal static unsafe class Runtime
    {
        public static void InstallCounter(MethodInfo method, Counter counter)
        {
            InstallCounter(method.DeclaringType.Assembly.GetName().Name, method.DeclaringType.Module.Name, method.DeclaringType.FullName, method.Name, counter);
        }

        private static unsafe void InstallCounter(string assembly, string module, string type, string method, Counter counter)
        {
            fixed (char* pAssembly = assembly)
            fixed (char* pModule = module)
            fixed (char* pType = type)
            fixed (char* pMethod = method)
            {
                var handle = (nint)NativeRuntime.InstallCounter((long)pAssembly, (long)pModule, (long)pType, (long)pMethod, counter.GetAddress());
                if (handle == 0)
                    throw new InvalidOperationException("Failed to install counter.");

                using (var signal = new NativeWaitHandle(handle))
                    signal.WaitOne();
            }
        }
    }

    internal static unsafe class NativeRuntime
    {
        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static long InstallCounter(long assemblyName, long moduleName, long typeName, long methodName, long callCountPointer)
        {
            throw new InvalidOperationException("The runtime is not loaded.");
        }
    }

    internal sealed class NativeWaitHandle : WaitHandle
    {
        public NativeWaitHandle(IntPtr handle)
        {
            if (handle == IntPtr.Zero || handle == InvalidHandle)
                SafeWaitHandle = null;
            else
                SafeWaitHandle = new SafeWaitHandle(handle, true);
        }
    }
}
