#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "KompexSQLitePrerequisites.h"  
#include "KompexSQLiteDatabase.h"  
#include "KompexSQLiteStatement.h"  
#include "KompexSQLiteException.h"  
#include "KompexSQLiteStreamRedirection.h"  
#include "KompexSQLiteBlob.h"
#include <glib.h>
#include "wlss.h"

extern "C" int sensor_data_info_update_db(const gchar *db, guint id) {
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase(db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);

		pStmt->Sql("UPDATE temporary_db SET status=0 WHERE id=@id;");
			
		// bind an integer to the prepared statement
		pStmt->BindInt(1, id);					// bind id
		
		// execute it and clean-up
        pStmt->ExecuteAndFree();
		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}
	return 0;
}

extern "C" int sensor_data_info_form_db(const gchar *db, guint **pdat) {
	int cnt = -1;
	int id = 0;
	
	*pdat = NULL;
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase(db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);

		int cnt = pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='temporary_db';");
		//std::cout << "COUNT(*): " << cnt << std::endl;
		if(cnt == 0) {
			return -1;
		}
		int i=0;
		pStmt->Sql("SELECT * FROM temporary_db WHERE status='1' ORDER BY id DESC LIMIT 1;"); //"SELECT * FROM temporary_db WHERE p_value='1.0';"
		while(pStmt->FetchRow()) {
			guint nums = pStmt->GetColumnInt("nums");
			guint alen = pStmt->GetColumnInt("alen");
			struct __data_st *_data;
			_data = (struct __data_st *) g_malloc((16  + nums) * 4);
			guint *pi = (guint *)_data;
			gfloat *pf = (gfloat *)_data;
			
			//printf("nums=%d\n", nums);
			//guint *p = (guint *)pStmt->GetColumnBlob("aflag");
			//std::cout << "aflag(*): " <<  p[0] << std::endl;
			//0:sid 1:ts 2:volt 3:rssi: 4:type 5:channel 6:freq 7:nums 8:alen 9:aflag[] 10:temperature 11:p-value 12:pp-value 13:rms 
			id = pStmt->GetColumnInt("id");
			_data->sid = pStmt->GetColumnInt("sid");
			_data->ts = pStmt->GetColumnInt("ts");
			_data->volt = pStmt->GetColumnInt("volt");
			_data->rssi = pStmt->GetColumnInt("rssi");
			_data->type = pStmt->GetColumnInt("type");
			_data->channel = pStmt->GetColumnInt("channel");
			_data->freq = pStmt->GetColumnInt("freq");
			_data->nums = nums;
			_data->alen = alen;
			guint *ptem = NULL;
			if(alen > 0) {
				ptem = (guint *)g_malloc0(alen * 4);
				memcpy((void *)&ptem[0], pStmt->GetColumnBlob("aflag"), alen * 4);
			}
			_data->aflag = GPOINTER_TO_UINT(ptem);
			_data->temp = pStmt->GetColumnDouble("temperature");
			_data->p_val = pStmt->GetColumnDouble("p_value");
			_data->pp_val = pStmt->GetColumnDouble("pp_value");
			_data->rms_val = pStmt->GetColumnDouble("rms_value");
			memcpy((void *)&_data->data, pStmt->GetColumnBlob("data"), nums * 4);
			*pdat = (guint*)_data;
			
			//printf("%p %f, %f, %f %f\n",ptem, pf[10], pf[11], pf[12], pf[13]);
			//printf("%u, %u, %u, %u, %u, %u, %u, %u, %u %x\n", pi[0], pi[1], pi[2], pi[3], pi[4], pi[5], pi[6], pi[7], pi[8], pi[9]);
		}
		// do not forget to clean-up
		pStmt->FreeQuery();

		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}
	
	return id;
}

extern "C" int sensor_data_info_insert_db(const gchar *db, guint id, gfloat *pfval, guint *pival) {
	//at beginning of pval:: 0:p-value 1:pp-value 2:rms 3:channel 4:freq 5:nums 6:ts 7:aflag 8:volt 9:rssi 10 type
	if(pfval == NULL) return -1;
	
	gfloat p_value = pfval[0];
	gfloat pp_value = pfval[1];
	gfloat rms_value = pfval[2];
	guint channel = pival[3];
	guint freq = pival[4];
	guint nums = pival[5];
	guint ts = pival[6];
	
	guint *aflag = (guint *)GUINT_TO_POINTER(pival[7]);
	guint volt = pival[8];
	guint rssi = pival[9];
	guint type = pival[10];
	gfloat temp = pfval[11];
	guint alen = pival[12];
	void *data = (void *)&pfval[16];
	guint dlen = nums * 4;
//printf("alen=%d aflag=%p :: %x\n", alen, aflag, aflag[0]);
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase(db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);
		
		int cnt = pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='temporary_db';");
		//std::cout << "COUNT(*): " << cnt << std::endl;
		if(cnt == 0) {
			pStmt->SqlStatement("CREATE TABLE temporary_db (id INTEGER PRIMARY KEY, status INTEGER, sid INTEGER, ts INTEGER, channel INTEGER, freq INTEGER, nums INTEGER, p_value DOUBLE, pp_value DOUBLE, rms_value DOUBLE, alen INTEGER, aflag BLOB, volt INTEGER, rssi INTEGER, type INTEGER, temperature DOUBLE, data BLOB)");
		}
		pStmt->Sql("INSERT INTO temporary_db (id, status, sid, ts, channel, freq, nums, p_value, pp_value, rms_value, alen, aflag, volt, rssi, type, temperature, data) \
			VALUES (@id, @status, @sid, @ts, @channel, @freq, @nums, @p_value, @pp_value, @rms_value, @alen, @aflag, @volt, @rssi, @type, @temperature, @data)");
		// here we go - the third round
		int i = 2;
		pStmt->BindNull(1);							// id
		pStmt->BindInt(i++, 1);						// status
		pStmt->BindInt(i++, id);						// sid
		pStmt->BindInt(i++, ts);						// ts
		pStmt->BindInt(i++, channel);			// channle
		pStmt->BindInt(i++, freq);					// freq
		pStmt->BindInt(i++, nums);				// nums
		pStmt->BindDouble(i++, p_value);	// p-value
		pStmt->BindDouble(i++, pp_value);	// pp-value
		pStmt->BindDouble(i++, rms_value);// rms-value
		pStmt->BindInt(i++, alen);					// alarm length
		pStmt->BindBlob(i++, aflag, alen * 4);	// alarm flag
		pStmt->BindInt(i++, volt);					// voltage
		pStmt->BindInt(i++, rssi);					// rssi
		pStmt->BindInt(i++, type);					// type
		pStmt->BindDouble(i++, temp);		// temperature
		pStmt->BindBlob(i++, data, dlen);	// data
		// execute the statement
		pStmt->Execute();
		// we don't need the prepared statement anymore so we clean-up everything
		pStmt->FreeQuery();
		
		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}
	return 0;
}
extern "C" int misc_info_from_db(const char *db, char **gid) {
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase(db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);

		//std::cout << "SQLite version: " << pDatabase->GetLibVersionNumber() << std::endl;
		//select count(*) from sqlite_master where type='table' and name='user0';
		int cnt = pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='misc_info';");
		//std::cout << "COUNT(*): " << cnt << std::endl;
		if(cnt == 0) {
			pStmt->SqlStatement("CREATE TABLE misc_info (gid VARCHAR(128))");
			pStmt->SqlStatement("INSERT INTO misc_info (gid) VALUES ('G00#default');");
		}

		pStmt->Sql("SELECT * FROM misc_info;");
		// process all results
		while(pStmt->FetchRow())
		{
			char *id = (char *)pStmt->GetColumnCString("gid");
			asprintf(gid, "%s", id);
			//std::cout << "gid: " << gid << std::endl;
		}
		// do not forget to clean-up
		pStmt->FreeQuery();
		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}
	
	return 0;
}
//, localip VARCHAR(64), localport INTEGER, serverip VARCHAR(64), serverport INTEGER, mqttip VARCHAR(64), mqttport INTEGER, user VARCHAR(64), password VARCHAR(64)
extern "C" int misc_info_insert_db(const char *db, char *gid) {
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase(db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);

		//std::cout << "SQLite version: " << pDatabase->GetLibVersionNumber() << std::endl;
		//select count(*) from sqlite_master where type='table' and name='user0';
		int cnt = pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='misc_info';");
		//std::cout << "COUNT(*): " << cnt << std::endl;
		if(cnt == 0) {
			pStmt->SqlStatement("CREATE TABLE misc_info (gid VARCHAR(128))");
			pStmt->SqlStatement("INSERT INTO misc_info (gid) VALUES ('G00#default');");
		}
		pStmt->Sql("UPDATE misc_info SET gid=@gid");	
		// bind an integer to the prepared statement
		pStmt->BindString(1, gid);					// bind id
		
		// execute it and clean-up
        pStmt->ExecuteAndFree();
		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}
	
	return 0;
}
extern "C" int platform_info_from_db(const char *db, int *channel, int *pan, int *power) {
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase(db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);

		//std::cout << "SQLite version: " << pDatabase->GetLibVersionNumber() << std::endl;
		//select count(*) from sqlite_master where type='table' and name='user0';
		int cnt = pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='platform_info';");
		//std::cout << "COUNT(*): " << cnt << std::endl;
		if(cnt == 0) {
			pStmt->SqlStatement("CREATE TABLE platform_info (subGchannel INTEGER, subGpan INTEGER, subGpa INTEGER)");
			pStmt->SqlStatement("INSERT INTO platform_info (subGchannel, subGpan, subGpa) VALUES (772,30,0);");
		}
		
		pStmt->Sql("SELECT * FROM platform_info;");
		// process all results
		while(pStmt->FetchRow())
		{
			*channel = pStmt->GetColumnInt("subGchannel");
			*pan = pStmt->GetColumnInt("subGpan");
			*power = pStmt->GetColumnInt("subGpa");
			std::cout << "channel: " << *channel << std::endl;
			std::cout << "pan: " << *pan << std::endl;
			std::cout << "power: " << *power << std::endl;
		}
		// do not forget to clean-up
		pStmt->FreeQuery();
		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}
	
	return 0;
}
extern "C" int sensor_volt_info_to_db(const char *db, int sid, int ts, int volt, int rssi)
{  
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase(db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);

		//std::cout << "SQLite version: " << pDatabase->GetLibVersionNumber() << std::endl;
		//select count(*) from sqlite_master where type='table' and name='user0';
		int cnt = pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='volt_info';");
		//std::cout << "COUNT(*): " << cnt << std::endl;
		if(cnt == 0) {
			pStmt->SqlStatement("CREATE TABLE volt_info (sid INTEGER, ts INTEGER, volt INTEGER, rssi INTEGER)");
		}
		char *sql = NULL;
		asprintf(&sql, "INSERT INTO volt_info (sid, ts, volt, rssi) VALUES (%d,%d,%d,%d);", sid, ts, volt, rssi);
		pStmt->SqlStatement(sql);
		free(sql);
		
		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}

	return 0;
}

#if 0
void FunctionWithLocalVariable(Kompex::SQLiteStatement *stmt);

int main()
{
	// uncomment to redirect streams to a file
	//Kompex::CerrRedirection cerrRedirection("error.log");
	//Kompex::CoutRedirection coutRedirection("output.log");
	float channel;
	int pan;
	int pa;
	//sqlite3_platform_info("/tmp/wlss.db", &channel, &pan, &pa);
	return 0;
	try
	{
		// create and open database
		Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase("test.db", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
		// create statement instance for sql queries/statements
		Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);

		std::cout << "SQLite version: " << pDatabase->GetLibVersionNumber() << std::endl;
		//select count(*) from sqlite_master where type='table' and name='user0';
		int cnt = pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='volt_info';");
		std::cout << "COUNT(*): " << cnt << std::endl;

		// ---------------------------------------------------------------------------------------------------------
		// create table and insert some data
		if(cnt == 0) {
		pStmt->SqlStatement("CREATE TABLE user (userID INTEGER NOT NULL PRIMARY KEY, lastName VARCHAR(50) NOT NULL, firstName VARCHAR(50), age INTEGER, weight DOUBLE)");
		}
		pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (1, 'Lehmann', 'Jamie', 20, 65.5)");
		pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (2, 'Burgdorf', 'Peter', 55, NULL)");
		pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (3, 'Lehmann', 'Fernando', 18, 70.2)");
		pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (4, 'Lehmann', 'Carlene ', 17, 50.8)");

		// ---------------------------------------------------------------------------------------------------------
		// insert some data with Bind..() methods
		pStmt->Sql("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES(?, ?, ?, ?, ?);");
		pStmt->BindInt(1, 5);
		pStmt->BindString(2, "Murahama");
		pStmt->BindString(3, "Yura");
		pStmt->BindInt(4, 28);
		pStmt->BindDouble(5, 60.2);
		// executes the INSERT statement and cleans-up automatically
		pStmt->ExecuteAndFree();

		// ---------------------------------------------------------------------------------------------------------
		// let's have a look on a query which is shown in console
		std::cout << std::endl;
		pStmt->GetTable("SELECT firstName, userID, age, weight FROM user WHERE lastName = 'Lehmann';", 13);
		std::cout << std::endl;

		// some example SQLite aggregate functions
		std::cout << "COUNT(*): " << pStmt->SqlAggregateFuncResult("SELECT COUNT(*) FROM user WHERE lastName = 'Lehmann';") << std::endl;
		std::cout << "COUNT(weight): " << pStmt->SqlAggregateFuncResult("SELECT COUNT(weight) FROM user;") << std::endl;
		std::cout << "MAX(age): " << pStmt->SqlAggregateFuncResult("SELECT MAX(age) FROM user;") << std::endl;
		std::cout << "MIN(age): " << pStmt->SqlAggregateFuncResult("SELECT MIN(age) FROM user;") << std::endl;
		std::cout << "AVG(age): " << pStmt->SqlAggregateFuncResult("SELECT AVG(age) FROM user;") << std::endl;
		std::cout << "SUM(age): " << pStmt->SqlAggregateFuncResult("SELECT SUM(age) FROM user;") << std::endl;
		std::cout << "TOTAL(age): " << pStmt->SqlAggregateFuncResult("SELECT TOTAL(age) FROM user;") << std::endl;

		// ---------------------------------------------------------------------------------------------------------

		// sql query - searching for all people with lastName "Lehmann"
		pStmt->Sql("SELECT firstName FROM user WHERE lastName = 'Lehmann';");

		// after a Sql() call we can get some special information
		std::cout << "\nGetColumnName: " << pStmt->GetColumnName(0) << std::endl;
		std::cout << "GetColumnCount: " << pStmt->GetColumnCount() << std::endl;
		std::cout << "GetColumnDatabaseName: " << pStmt->GetColumnDatabaseName(0) << std::endl;
		std::cout << "GetColumnTableName: " << pStmt->GetColumnTableName(0) << std::endl;
		std::cout << "GetColumnOriginName: " << pStmt->GetColumnOriginName(0) << "\n" << std::endl;

		// do not forget to clean-up
		pStmt->FreeQuery();
		
		// ---------------------------------------------------------------------------------------------------------
		// another sql query
		pStmt->Sql("SELECT * FROM user WHERE firstName = 'Jamie';");

		// after a Sql() call we can get some other special information
		std::cout << "GetColumnName(0): " << pStmt->GetColumnName(0) << std::endl;
		std::cout << "GetColumnName(1): " << pStmt->GetColumnName(1) << std::endl;
		std::cout << "GetColumnName(2): " << pStmt->GetColumnName(2) << std::endl;
		std::cout << "GetColumnName(3): " << pStmt->GetColumnName(3) << std::endl;
		std::cout << "GetColumnName(4): " << pStmt->GetColumnName(4) << std::endl;
		std::cout << "GetColumnCount: " << pStmt->GetColumnCount() << std::endl;	
		std::cout << "GetColumnDeclaredDatatype(0): " << pStmt->GetColumnDeclaredDatatype(0) << std::endl;
		std::cout << "GetColumnDeclaredDatatype(1): " << pStmt->GetColumnDeclaredDatatype(1) << std::endl;
		std::cout << "GetColumnDeclaredDatatype(2): " << pStmt->GetColumnDeclaredDatatype(2) << std::endl;
		std::cout << "GetColumnDeclaredDatatype(3): " << pStmt->GetColumnDeclaredDatatype(3) << std::endl;
		std::cout << "GetColumnDeclaredDatatype(4): " << pStmt->GetColumnDeclaredDatatype(4) << std::endl;

		// process all results
		while(pStmt->FetchRow())
		{
			std::cout << "\nGetDataCount: " << pStmt->GetDataCount() << std::endl;
			std::cout << "SQL query - GetColumnDouble(0): " << pStmt->GetColumnDouble(0) << std::endl;
			std::cout << "SQL query - GetColumnString(1): " << pStmt->GetColumnString(1) << std::endl;
			std::cout << "SQL query - GetColumnString(2): " << pStmt->GetColumnString(2) << std::endl;
			std::cout << "SQL query - GetColumnString(3): " << pStmt->GetColumnString(3) << std::endl;
			std::cout << "SQL query - GetColumnString(4): " << pStmt->GetColumnString(4) << std::endl;
			std::cout << "\nColumnTypes (look at the documentation for the meaning of the numbers):\n";
			std::cout << "GetColumnType(0): " << pStmt->GetColumnType(0) << std::endl;
			std::cout << "GetColumnType(1): " << pStmt->GetColumnType(1) << std::endl;
			std::cout << "GetColumnType(2): " << pStmt->GetColumnType(2) << std::endl;
			std::cout << "GetColumnType(3): " << pStmt->GetColumnType(3) << std::endl;
			std::cout << "GetColumnType(4): " << pStmt->GetColumnType(4) << std::endl;
		}

		// do not forget to clean-up
		pStmt->FreeQuery();

		// ---------------------------------------------------------------------------------------------------------
		// a little example how to get some queried data via column name
		std::cout << "\nGet some queried data via column name:\n";
		pStmt->Sql("SELECT * FROM user WHERE lastName = 'Lehmann';");

		// process all results
		while(pStmt->FetchRow())
		{
			std::cout << "firstName: " << pStmt->GetColumnString("firstName") << std::endl;
			std::cout << "age: " << pStmt->GetColumnInt("age") << std::endl;
		}

		// do not forget to clean-up
		pStmt->FreeQuery();
#if 0
		// ---------------------------------------------------------------------------------------------------------
		// example for prepared statements - repetitive execution (SELECT)
		std::cout << "\nPrepared statement - repetitive execution (SELECT):\n";
		pStmt->Sql("SELECT * FROM user WHERE userID=@id");
			
		for(int i = 1; i <= 3; ++i)
		{
			// bind an integer to the prepared statement
			pStmt->BindInt(1, i);

			// and now fetch all results
			while(pStmt->FetchRow())
				std::cout << pStmt->GetColumnCString(0) << " " << pStmt->GetColumnCString(1) << std::endl;

			// reset the prepared statement
			pStmt->Reset();
		}
		// do not forget to clean-up
		pStmt->FreeQuery();

		// ---------------------------------------------------------------------------------------------------------
		// example for prepared statements - repetitive execution (INSERT/UPDATE/DELETE)
		std::cout << "\nPrepared statement - repetitive execution (INSERT/UPDATE/DELETE):\n";
		std::cout << "no output here - there are only some INSERTs ;)\n";

		// create a table structure
		pStmt->SqlStatement("CREATE TABLE flower (flowerID INTEGER NOT NULL PRIMARY KEY, name VARCHAR(50) NOT NULL, size DOUBLE)");
		// create the prepared statement
		pStmt->Sql("INSERT INTO flower (flowerID, name, size) VALUES (@flowerID, @name, @size)");
			
		// bind all three values
		pStmt->BindInt(1, 1);					// flowerID
		pStmt->BindString(2, "rose");			// name
		pStmt->BindDouble(3, 50.5);				// size
		// execute the statement and reset the bindings
		pStmt->Execute();
		pStmt->Reset();

		// here we go - the second round
		pStmt->BindInt(1, 2);					// flowerID
		pStmt->BindString(2, "primrose");		// name
		pStmt->BindDouble(3, 6.21);				// size
		// execute the statement and reset the bindings
		pStmt->Execute();
		pStmt->Reset();
	
		// here we go - the third round
		pStmt->BindInt(1, 3);					// flowerID
		pStmt->BindString(2, "rhododendron");	// name
		pStmt->BindDouble(3, 109.84);			// size
		// execute the statement
		pStmt->Execute();
		// we don't need the prepared statement anymore so we clean-up everything
		pStmt->FreeQuery();
		
		// ---------------------------------------------------------------------------------------------------------
		// two possibilities to update data in the database

		// the first way
		std::cout << "\nUPDATE possibility 1: prepared statement - single execution";
		pStmt->Sql("UPDATE user SET lastName=@lastName, age=@age WHERE userID=@userID");
			
		// bind an integer to the prepared statement
        pStmt->BindString(1, "Urushihara");		// bind lastName
		pStmt->BindInt(2, 56);					// bind age
		pStmt->BindInt(3, 2);					// bind userID

		// execute it and clean-up
        pStmt->ExecuteAndFree();

		// the second way
		std::cout << "\nUPDATE possibility 2: common statement\n";
		pStmt->SqlStatement("UPDATE user SET weight=51.5, age=18 WHERE firstName='Carlene'");

		// ---------------------------------------------------------------------------------------------------------
		// get some instant results
		std::cout << "\nSELECT lastName FROM user WHERE userID = 3;\n" << pStmt->GetSqlResultString("SELECT lastName FROM user WHERE userID = 3");
		std::cout << "\nSELECT age FROM user WHERE userID = 4;\n" <<  pStmt->GetSqlResultInt("SELECT age FROM user WHERE userID = 4");
		std::cout << "\nSELECT weight FROM user WHERE userID = 3;\n" <<  pStmt->GetSqlResultDouble("SELECT weight FROM user WHERE userID = 3");

		// don't forget to delete the pointer for all GetSqlResult%() methods which return a pointer
		const unsigned char *lastName = pStmt->GetSqlResultCString("SELECT lastName FROM user WHERE userID = 2");
		// do something with lastName
		delete[] lastName;

		// ---------------------------------------------------------------------------------------------------------
		// DELETE statement and get afterwards the number of affected rows
		pStmt->SqlStatement("DELETE FROM user WHERE lastName = 'Lehmann'");
		std::cout << "\n\nGetDatabaseChanges: " << pDatabase->GetDatabaseChanges() << std::endl;

		// let's see, how many changes we have done
		std::cout << "GetTotalDatabaseChanges: " << pDatabase->GetTotalDatabaseChanges() << std::endl;
		std::cout << std::endl;

		// ---------------------------------------------------------------------------------------------------------
		// get all metadata from one column
		pStmt->GetTableColumnMetadata("user", "userID");
		std::cout << std::endl;

		// ---------------------------------------------------------------------------------------------------------
		// now we want try a transaction
		// if an error occurs, a rollback is automatically performed
		// note: you must use Transaction()
		pStmt->BeginTransaction();
		pStmt->Transaction("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (10, 'Kanzaki', 'Makoto', 23, 76.9)");
		FunctionWithLocalVariable(pStmt);
		pStmt->Transaction("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (12, 'Kanzaki', 'Peter', 63, 101.1)");
		pStmt->CommitTransaction();

		// if you want react on errors by yourself, you can use an own try() and catch() block
		// note: you must use SqlStatement()
		try
		{
			pStmt->BeginTransaction();
			pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (10, 'Kanzaki', 'Makoto', 23, 76.9)");
			pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (11, 'Kanzaki', 'Yura', 20, 56.9)");
			pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (12, 'Kanzaki', 'Peter', 63, 101.1)");
			pStmt->SqlStatement("INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (10, 'Henschel', 'Robert', 10, 34.5)");
			pStmt->CommitTransaction();
		}
		catch(Kompex::SQLiteException &exception) 
		{
			std::cerr << "Exception Occured: " << exception.GetString();
			std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
			pStmt->RollbackTransaction();
			std::cerr << "Rollback has been executed!" << std::endl;
			std::cerr << "This is our own catch() block!" << std::endl;
		}

		// ---------------------------------------------------------------------------------------------------------
		// Kompex::SQLiteBlob example
		std::cout << "\nKompex::SQLiteBlob example\n";

		// create a table and fill it with some dummy data 
		// the content of the BLOBs is simple plaintext so that you can see what happens
		pStmt->SqlStatement("CREATE TABLE boarduser (userID INTEGER NOT NULL PRIMARY KEY, username VARCHAR(20), picture BLOB)");
		pStmt->SqlStatement("INSERT INTO boarduser (userID, username, picture) VALUES (1, 'apori', 'abcdefghijklmnopqrstuvwxyz')");
		pStmt->SqlStatement("INSERT INTO boarduser (userID, username, picture) VALUES (2, 'sarina', 'abcdefghijklmnopqrstuvwxyz')");

		// open the existing BLOB for user 'apori'
		Kompex::SQLiteBlob *pKompexBlob = new Kompex::SQLiteBlob(pDatabase, "main", "boarduser", "picture", 1);
		// get the size of the current BLOB
		std::cout << "GetBlobSize(): " << pKompexBlob->GetBlobSize() << std::endl;

		// read the whole BLOB value
		int blobSize = pKompexBlob->GetBlobSize();
		char *readBuffer = new char[blobSize + 1];
		readBuffer[blobSize] = '\0';
		pKompexBlob->ReadBlob(readBuffer, blobSize);
		std::cout << "ReadBlob() output: " << readBuffer << std::endl;
		
		// overwrite a part of the BLOB
		std::cout << "WriteBlob() - change the BLOB data\n";
		char newData[8] = "-HELLO-";
		pKompexBlob->WriteBlob(newData, sizeof(newData) - 1, 10);

		// and read the whole BLOB value again
		pKompexBlob->ReadBlob(readBuffer, blobSize);
		std::cout << "ReadBlob() output: " << readBuffer << std::endl;
		delete readBuffer;

		delete pKompexBlob;

		// ---------------------------------------------------------------------------------------------------------
		// create a sql statement with SQLite functions
		const char *param1 = "It's a happy day.";
		const char *param2 = "Muhahaha!";
		std::cout << "\n" << pStmt->Mprintf("INSERT INTO table VALUES('%q', '%q')", param1, param2) << std::endl;

		// ---------------------------------------------------------------------------------------------------------
		// statistics
		std::cout << "\nGetNumberOfCheckedOutLookasideMemorySlots: " << pDatabase->GetNumberOfCheckedOutLookasideMemorySlots() << std::endl;
		std::cout << "GetHighestNumberOfCheckedOutLookasideMemorySlots: " << pDatabase->GetHighestNumberOfCheckedOutLookasideMemorySlots() << std::endl;
		std::cout << "GetHeapMemoryUsedByPagerCaches: " << pDatabase->GetHeapMemoryUsedByPagerCaches() << std::endl;
		std::cout << "GetHeapMemoryUsedToStoreSchemas: " << pDatabase->GetHeapMemoryUsedToStoreSchemas() << std::endl;
		std::cout << "GetHeapAndLookasideMemoryUsedByPreparedStatements: " << pDatabase->GetHeapAndLookasideMemoryUsedByPreparedStatements() << std::endl;
		std::cout << "GetPagerCacheHitCount: " << pDatabase->GetPagerCacheHitCount() << std::endl;
		std::cout << "GetPagerCacheMissCount: " << pDatabase->GetPagerCacheMissCount() << std::endl;
		std::cout << "GetNumberOfDirtyCacheEntries: " << pDatabase->GetNumberOfDirtyCacheEntries() << std::endl;
		std::cout << "GetNumberOfUnresolvedForeignKeys: " << pDatabase->GetNumberOfUnresolvedForeignKeys() << std::endl;
		std::cout << "GetLookasideMemoryHitCount: " << pDatabase->GetLookasideMemoryHitCount() << std::endl;
		std::cout << "GetLookasideMemoryMissCountDueToSmallSlotSize: " << pDatabase->GetLookasideMemoryMissCountDueToSmallSlotSize() << std::endl;
		std::cout << "GetLookasideMemoryMissCountDueToFullMemory: " << pDatabase->GetLookasideMemoryMissCountDueToFullMemory() << std::endl;
#endif
		// ---------------------------------------------------------------------------------------------------------
		// clean-up
		delete pStmt;
		delete pDatabase;
	}
	catch(Kompex::SQLiteException &exception)
	{
		std::cerr << "\nException Occured" << std::endl;
		exception.Show();
		std::cerr << "SQLite result code: " << exception.GetSqliteResultCode() << std::endl;
	}

	/*
	// complete example for the usage of file and memory databases
	// (database layout is only fictitious)

	Kompex::SQLiteDatabase *pDatabase = new Kompex::SQLiteDatabase("scores.db", SQLITE_OPEN_READWRITE, 0);
	// move database to memory, so that we are working on the memory database
	pDatabase->MoveDatabaseToMemory();

	Kompex::SQLiteStatement *pStmt = new Kompex::SQLiteStatement(pDatabase);
	// insert some data sets into the memory database
	pStmt->SqlStatement("INSERT INTO score(id, lastScore, avgScore) VALUES(1, 429, 341)");
	pStmt->SqlStatement("INSERT INTO score(id, lastScore, avgScore) VALUES(2, 37, 44)");
	pStmt->SqlStatement("INSERT INTO score(id, lastScore, avgScore) VALUES(3, 310, 280)");

	// save the memory database to a file
	// if you don't do it, all database changes will be lost after closing the memory database
	pDatabase->SaveDatabaseFromMemoryToFile("newScores.db");

	delete pStmt;
	delete pDatabase;
	*/

	std::cin.get();
	return 0;
}

void FunctionWithLocalVariable(Kompex::SQLiteStatement *stmt)
{
	const wchar_t *localVariable = L"INSERT INTO user (userID, lastName, firstName, age, weight) VALUES (11, 'Kanzaki', 'Yura', 20, 56.9)";
	// use SecureTransaction() which creates a internal copy of localVariable
	stmt->SecureTransaction(localVariable);
	// localVariable will be deleted on the end of this scope!
}
#endif
