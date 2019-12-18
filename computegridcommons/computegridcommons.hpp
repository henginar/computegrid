#pragma once

#include <QString>
#include <QStringList>

namespace ComputeGrid
{
#pragma region Enums
	enum LogSource
	{
		LS_GM,	// Grid-Manager
		LS_GW,	// Grid-Worker
		LS_MP,	// Manager-Process
		LS_WP	// Worker-Process
	};

	enum LogType
	{
		LT_INFO,
		LT_WARNING,
		LT_ERROR
	};

	enum DataPacketType
	{
		DPT_HEARTHBEAT	= 1,
		DPT_GRID_ATTACH,		// [GM > GW] rawData=workerProcessData
		DPT_GRID_WORKER_READY,	// [GW > GM] no args
		DPT_WORKER_DATA,		// [GM <> GW] p1=worker, p2..pN=work spesific args
		DPT_WORKER_EXIT,		// [GW <> GM] p1=worker, (GM> p2..pN=work spesific args) || (GW> p2=exitCode, p3=exitStatus)
		DPT_LOG					// [GW > GM] p1=LogSource, p2=LogType, p3=logMessage
	};

	enum ProcessCommand
	{
		PC_GRID_WORKER_IN,		// [GM > MP || GW > WP] (GM> p1=worker p2=ideal_thread_count_of_worker) || (GW> no args)
		PC_GRID_WORKER_OUT,		// [GM > MP || GW > WP] (GM> p1=worker) || (GW> no args)
		PC_WORKER_DATA,			// [WP <> MP] p1=worker, p2..pN=work spesific args
		PC_WORKER_EXIT,			// [MP > WP || GW > MP] p1=worker, (MP> p2..pN=work spesific args) || (GW> p2=exitCode, p3=exitStatus)
		PC_LOG,					// [WP > GM || MP > GM] p1=LogSource, p2=LogType, p3=logMessage
		PC_STATUS_MESSAGE,		// [MP > GM || WP > GW] p1=Message
		PC_TERMINAL_COMMAND		// [GM > MP] p1..pN=work spesific args
	};
#pragma endregion

#pragma region Literals
	static QStringList LiteralLogSource = QStringList()
		<< "Grid Manager"
		<< "Grid Worker"
		<< "Manager Process"
		<< "Worker Process";

	static QStringList LiteralLogType = QStringList()
		<< "Info"
		<< "Warning"
		<< "Error";

	static QStringList LiteralProcessCommand = QStringList()
		<< "wig"
		<< "wog"
		<< "wd"
		<< "wex"
		<< "log"
		<< "stm"
		<< "tc";


	static QStringList LiteralSocketError = QStringList()
		<< "ConnectionRefusedError"
		<< "RemoteHostClosedError"
		<< "HostNotFoundError"
		<< "SocketAccessError"
		<< "SocketResourceError"
		<< "SocketTimeoutError"
		<< "DatagramTooLargeError"
		<< "NetworkError"
		<< "AddressInUseError"
		<< "SocketAddressNotAvailableError"
		<< "UnsupportedSocketOperationError"
		<< "UnfinishedSocketOperationError"
		<< "ProxyAuthenticationRequiredError"
		<< "SslHandshakeFailedError"
		<< "ProxyConnectionRefusedError"
		<< "ProxyConnectionClosedError"
		<< "ProxyConnectionTimeoutError"
		<< "ProxyNotFoundError"
		<< "ProxyProtocolError"
		<< "OperationError"
		<< "SslInternalError"
		<< "SslInvalidUserDataError"
		<< "TemporaryError";
#pragma endregion

	class ComputeGridGlobals
	{
	public:
#pragma region Helper Methods
		static QString makeProcessCommand(ProcessCommand _pc, QString _arg = QString())
		{
			QStringList sl;
			if (!_arg.isEmpty())
				sl.append(_arg);

			return makeProcessCommand(_pc, sl);
		}

		static QString makeProcessCommand(ProcessCommand _pc, QStringList _args = QStringList())
		{
			QString cmd = QString("%1%2").arg(ProcessCommandPrefix).arg(LiteralProcessCommand[_pc]);

			for (QStringList::iterator it = _args.begin(); it != _args.end(); ++it)
				cmd += ProcessCommandSeperator + (*it).replace(ProcessCommandSeperator, ProcessCommandDataSeperator);

			return cmd;
		}

		static QString makeLogCommand(LogSource _logSource, LogType _logType, QString _message)
		{
			return makeProcessCommand(
				PC_LOG,
				QStringList() << QString::number(_logSource) << QString::number(_logType) << _message
			);
		}

		static bool parseProcessCommand(const QString & _cmd, ProcessCommand & _pc, QStringList & _args)
		{
			bool res = false;

			if (_cmd.startsWith(ProcessCommandPrefix))
			{
				QStringList & sl = const_cast<QString &>(_cmd).remove(0, 1).split(ProcessCommandSeperator);

				if (sl.size() > 0)
				{
					int pci = LiteralProcessCommand.indexOf(sl[0]);
					if (res = (pci >= 0))
					{
						_pc = (ProcessCommand)pci;

						for (size_t i = 1; i < sl.count(); ++i)
							_args.append(sl[i]);
					}
				}
			}

			return res;
		}
#pragma endregion

#pragma region Fields
		static constexpr QChar ProcessCommandPrefix = '$';
		static constexpr QChar ProcessCommandSuffix = '\n';
		static constexpr QChar ProcessCommandSeperator = '|';
		static constexpr QChar ProcessCommandDataSeperator = '#';
#pragma endregion

	private:
		ComputeGridGlobals() { /* private ctor! */ }
	};
}