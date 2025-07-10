using EnvDTE;
using EnvDTE80;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using System;
using System.ComponentModel.Design;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Task = System.Threading.Tasks.Task;

namespace VSPyExpand
{
	internal sealed class PyExpandCommand
	{
		public const int CommandId = 0x0100;
		public static readonly Guid CommandSet = new Guid("02bf74f6-bc61-4067-8801-abce14fb0c9b");
		private readonly AsyncPackage package;
		private PyExpandCommand(AsyncPackage package, OleMenuCommandService commandService)
		{
			this.package = package ?? throw new ArgumentNullException(nameof(package));
			commandService = commandService ?? throw new ArgumentNullException(nameof(commandService));

			var menuCommandID = new CommandID(CommandSet, CommandId);
			var menuItem = new MenuCommand(this.Execute, menuCommandID);
			commandService.AddCommand(menuItem);
		}

		public static PyExpandCommand Instance
		{
			get;
			private set;
		}

		private Microsoft.VisualStudio.Shell.IAsyncServiceProvider ServiceProvider
		{
			get { return this.package; }
		}

		public static async Task InitializeAsync(AsyncPackage package)
		{
			// Switch to the main thread - the call to AddCommand in PyExpandCommand's constructor requires
			// the UI thread.
			await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync(package.DisposalToken);

			OleMenuCommandService commandService = await package.GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
			Instance = new PyExpandCommand(package, commandService);
		}

		private void Execute(object sender, EventArgs e)
		{
			ThreadHelper.ThrowIfNotOnUIThread();
			
			DTE2 dte = (DTE2)Package.GetGlobalService(typeof(DTE));
			Document activeDoc = dte?.ActiveDocument;

			if (activeDoc == null)
			{
				VsShellUtilities.ShowMessageBox(package, "No active document found.", "Run Python", OLEMSGICON.OLEMSGICON_WARNING, OLEMSGBUTTON.OLEMSGBUTTON_OK, OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
				return;
			}

			string filePath = activeDoc.FullName;

			// Save the document
			if (activeDoc.Saved == false)
			{
				activeDoc.Save();
				System.Threading.Thread.Sleep(50);
			}

			// Run the Python file
			try
			{
				ProcessStartInfo startInfo = new ProcessStartInfo
				{
					FileName = "PyExpand",
					Arguments = $"\"{filePath}\"",
					UseShellExecute = false,
					RedirectStandardOutput = true,
					RedirectStandardError = true,
					CreateNoWindow = true
				};

				System.Diagnostics.Process process = new System.Diagnostics.Process
				{
					StartInfo = startInfo
				};

				process.OutputDataReceived += (s, evt) => Debug.WriteLine(evt.Data);
				process.ErrorDataReceived += (s, evt) => Debug.WriteLine(evt.Data);

				process.Start();
				process.BeginOutputReadLine();
				process.BeginErrorReadLine();
				process.WaitForExit();
				process.Close();
			}
			catch (Exception ex)
			{
				VsShellUtilities.ShowMessageBox(package, $"Error running Python script:\n{ex.Message}", "Run Python", OLEMSGICON.OLEMSGICON_CRITICAL, OLEMSGBUTTON.OLEMSGBUTTON_OK, OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
			}
		}
		//{
		//	ThreadHelper.ThrowIfNotOnUIThread();
		//	string message = string.Format(CultureInfo.CurrentCulture, "Inside {0}.MenuItemCallback()", this.GetType().FullName);
		//	string title = "PyExpandCommand";
		//
		//	VsShellUtilities.ShowMessageBox(
		//		this.package,
		//		message,
		//		title,
		//		OLEMSGICON.OLEMSGICON_INFO,
		//		OLEMSGBUTTON.OLEMSGBUTTON_OK,
		//		OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
		//}
	}
}
