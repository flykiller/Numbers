#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_NONSTDC_NO_WARNINGS 1
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cassert>
#include <memory.h>
#include <ctime>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include <sstream>
#include <thread>
#include <mutex>
#include <queue>
#include "bn_new.h"
#include "hash.h"
#include <fcntl.h>
#ifdef _WIN64
#include <Windows.h>
#include <filesystem>
#include <io.h>
#else
#include <dirent.h>
#include <sys/dir.h>
#include <unistd.h>
#include <sys/resource.h>
#define _lseeki64 lseek
#include <pthread.h>
#include "findfirst.h"
#endif


//ДОДЕЛАТЬ:
//1) Быстрое деление - папа. Готово!
//2) Дальнейшее разбиение файла на более мелкие
//3) Оптимизировать код (профайлер?)
//4) Причесать не очень красивые массивы вычетов

using namespace std;

const int EVALUATE_LOGS = 1;					//Вычислять ли логарифмы и обратные степени в поиске сложного решения.
const int DO_REVERSE_ROOTS = 0;					//Использовать ли обратные корни в умном поиске.
const int DO_HASH_SQUARES = 1;					//Использовать для хэширования квадратов, аккуратнее с переполнением!
const int NUM_THREADS = 8;
using bn = BN;
using bnn = BN;

//Вид чисел, с которым мы будем работать (rootional number).
struct Triple {
	bn n;  //numerator			числитель
	bn m;  //denominator		знаменатель
	int k; //rootator			коренатель
    Triple(bn const &n = -1, bn const &m = -1, int k = -1): n(n), m(m), k(k) {

    }
    void print() {
        printf("(%s, %s, %d)\n", n.to_string(16).c_str(), m.to_string(16).c_str(), k);
    }

    Triple(string const &init) {
        char buf[10000];
        strcpy(buf, init.c_str());
        char *e = strchr(buf+1, '@');
        if (e != nullptr) {
            *e = 0;
            n = bn(buf);
            *e = '@';
            char *e1 = strchr(e + 1, '@');
            if (e1 != nullptr) {
                *e1 = 0;
                m = bn(e);
                k = atoi(e1 + 1);
                //printf("%s-> ", init.c_str()); print();
            }
#if 0
            char bbb[20000];
            sprintf(bbb, "@%s@%s@%d", n.to_string(16).c_str(), m.to_string(16).c_str(), k);
            if (init != bbb) {
                printf("init=%s != bbb=%s\n", init.c_str(), bbb);
            }
#endif
            return;
        }
        throw("Bad triple init");
    }
};


// Предопределённые массивы квадратичных вычетов для более быстрого выполнения проверки числа на квадратность.
static vector<int> quadratic569;
static vector<int> quadratic647;
static vector<int> quadratic653;
static vector<int> quadratic659;

static int digit = 2;							        //Цифра, которую мы используем.
static int depth = 7;							        //Глубина, для которой мы предпосчитали.
static const int MAXDEPTH = 12;
static int BigN = 1000;			    	    			//Константа, до которой мы будем искать решения.
static const int MAX_BIGN = 10000;                      //

static const int NumOfRoots = 5;						//Количесто раз, которые можно брать корень.
static const int LIMIT_log = 2048;						//Максимальное количество бит в числе.
static const int LIMIT_log_log = 11;					//Теоретическое количество раз, которое можно извлечь корень.
static bn tw = 2;
static const bn LIMIT = tw.pow_to(LIMIT_log);			//Максимальное возможное число.
static bn MaxNumbersForPowers[LIMIT_log + 1];			//Определяем глобальный массив, который заполним в функции, для RatPower.


static pht smallSolTriple[MAXDEPTH];

static string ArrayOfSolutions[MAX_BIGN * 10 + 1];			//Массив, куда мы будем записывать решения.
static int ArrayOfNumDigits[MAX_BIGN * 10 + 1];			//Массив, куда мы будем записывать количество цифр в решении.
static bn ArrayOfFactorials[LIMIT_log];				//Массив с предпосчитанными факториалами.
static const long long int FILESIZE = 1000000000;		//Максимальный размер файла, который мы будем разбивать. (не реализовано @@@@@)
static const int NumberOfLines = 1000;					//Количество строчек в файле на разбивку.

static map<bn, bn> ReadySquares;						//Предпосчитанынй массив вида (a^2,a); (!!УБЕДИТЬСЯ, ЧТО ОН НЕ СЖИРАЕТ ВСЮ ПАМЯТЬ)




static bool operator < (const Triple& left, const Triple& right) {
	int c = left.n.cmp(right.n);
	if (c < 0) {
		return true;
	}
	if (c > 0) {
		return false;
	}
	c = left.m.cmp(right.m);
	if (c < 0) {
		return true;
	}
	if (c > 0) {
		return false;
	}
	return left.k < right.k;
}

#if 0
static bool operator == (const Triple& left, const Triple& right) {
	if ((left.n == right.n) && (left.m == right.m) && (left.k == right.k)) {
		return true;
	}
	return false;
}
#endif


struct TripleTriple {
	Triple Triple1;				// первая тройка
	Triple Triple2;				// вторая тройка
	int		operation;			// номер операции
};                    

static string canonifyName(string const &base) {
    return string("Digit") + to_string(digit) + "/" + base;
}

//Функция для перевода Triple в string
static string TripleToString(Triple const &Triple1) {
	stringstream s;
	s << "@" << Triple1.n.to_string() << " @" << Triple1.m.to_string() << " " << to_string(Triple1.k);
	return s.str();
}


static void GenerateModules(int N, vector<int> &Res) {
	for (int i = 0; i < N; i++) {
		Res.push_back(-1);
	}
	Res[0] = 0;
	for (int i = 1; i < N; i++) {
		Res[(i*i) % N] = 1;
	}
}

//Обнулить массив с решениями и факториалы.
static void EmptyArraySol() {
	for (int i = 1; i <= BigN * 10; i++) {
		ArrayOfSolutions[i] = "!!!";
		ArrayOfNumDigits[i] = -1;
	}
	for (int i = 0; i < LIMIT_log; i++) {
		ArrayOfFactorials[i] = -1;
	}
}


// Функция, которая для длинного числа, проверяет, является ли оно квадратом.
// Если да, то возвращается пара(1, корень из числа), иначе(-1, -1)
static pair <int, bn> IsSquare(bn const &Numb) {

	if (quadratic569[Numb % 569] == -1) {
		return{ -1,-1 };
	}

	if (quadratic647[Numb % 647] == -1) {
		return{ -1,-1 };
	}

	if (quadratic653[Numb % 653] == -1) {
		return{ -1,-1 };
	}

	if (quadratic659[Numb % 659] == -1) {
		return{ -1,-1 };
	}

	if (DO_HASH_SQUARES == 1) {
		auto r = ReadySquares.find(Numb);
		if (r != ReadySquares.end()) {
			return{ 1,r->second };
		}
	}

	bn sqrtt = Numb;
	sqrtt.root_to(2);
	bn t = sqrtt*sqrtt;
	if (t == Numb) {
		//printf("FOUND SQUARE IS %s\n", sqrtt.to_string().c_str());
		if (DO_HASH_SQUARES == 1) {
			ReadySquares[Numb] = sqrtt;
		}
		return{ 1, sqrtt };
	}
	else {
		return{ -1,-1 };
	}
}

static int pow(int x, int power) {
	if (power == 0) return 1;
	int q = x;
	if (power & 1) {
		q *= pow(x, power - 1);
	}
	else {
		q = pow(x, power / 2);
		q *= q;
	}
	return q;
}

// Функция, которая проверяет, является ли число вида k^(power), где power это степень двойки: то есть квадратом, 4 степенью, 8 степенью и так далее.
//Если да, то возвращается пара (1, k), иначе возвращаем (-1, -1)
static pair <int, bn> isPower(bn const &a, int power) {
	bn x(-1);
	if (power == 1) {
		return{ 1,a };
	}
	else {
		int k;
		bn l;
		pair <int, bn> temp;
		temp = IsSquare(a);
		k = temp.first;
		l = temp.second;
		if (k == -1) {
			return{ -1,x };
		}
		else {
			return isPower(l, power / 2);
		}
	}
}

// Функция сложения 2 троек (n1, m1, k1) и (n2, m2, k2), возвращает новую тройку, если она в пределах допустимого, иначе (-1, -1, -1).
static struct Triple RatSum(struct Triple const &Triple1, struct Triple const &Triple2)
{

	Triple Triple3;
	bn m1, n1, m2, n2;
	int k1, k2;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	m2 = Triple2.m;
	n2 = Triple2.n;
	k2 = Triple2.k;
	if (k2 != k1) {
		return{ -1,-1,-1 };
	}
	if ((k1 == 0) && (k2 == 0)) {
		bn m3, n3;
		bn n1m2 = n1*m2;
		bn n2m1 = n2*m1;
		bn m1m2 = m1*m2;
		bn n1m2_n2m1 = n1m2 + n2m1;
		if (n1m2_n2m1 <= 0) {
			return{ -1,-1,-1 };
		}
		bn gcdd = bn::gcd(n1m2_n2m1, m1m2);

		n3 = n1m2_n2m1 / gcdd;
		m3 = m1m2 / gcdd;

		if ((n3 <= LIMIT) && (m3 <= LIMIT)) {
			return{ n3,m3,0 };
		}
		else {
			return{ -1,-1,-1 };
		}
	}

	if ((k1 == k2) && (k1 != 0)) {
		bn n1m2, n2m1, t1, test1, test2, sum2terms;
		n1m2 = n1*m2;
		n2m1 = n2*m1;
		sum2terms = n1m2 + n2m1;
		if (sum2terms <= 0) {
			return{ -1, -1, -1 };			//проверка того, что сумма не отрицательна
		}
		n2m1.abs();
		t1 = bn::gcd(n1m2, n2m1);
		test1 = n1m2 / t1;
		test2 = n2m1 / t1;

		//чтобы числа можно было складывать, нужно для чисел test1 и test2 произвести проверку на степенность

		pair <int, bn> pair1;
		pair1 = isPower(test1, pow(2, k1));
		if (pair1.first == -1) {
			return{ -1,-1,-1 };
		}

		pair <int, bn> pair2;
		pair2 = isPower(test2, pow(2, k1));
		if (pair2.first == -1) {
			return{ -1,-1,-1 };
		}

		bn Nsqrt, Msqrt, NMsqrt, ss, pow1, pow2, NNew, MNew, n3, m3, DNew;
		int k12 = pow(2, k1);
		ss = 2;
		Nsqrt = pair1.second;
		Msqrt = pair2.second;

		if (n2.sign() == 1) {
			NMsqrt = Nsqrt + Msqrt;
		}
		else {
			NMsqrt = Nsqrt - Msqrt;
		}

		int NMsqrtBits = NMsqrt.bits();
		if ((NMsqrtBits - 1)*k12 >= LIMIT_log + 2) {	//324234234234
			return{ -1,-1,-1 };
		}

		int MsqrtBits = Msqrt.bits();
		if ((MsqrtBits - 1)*k12 >= LIMIT_log + 2) {
			return{ -1,-1,-1 };
		}

		NMsqrt.pow_to(k12);
		Msqrt.pow_to(k12);

		n2.abs();		//берём модуль, чтобы в случае, когда было вычитание, ответ был положительным

		bn s1 = bn::gcd(NMsqrt, m2);
		bn s2 = bn::gcd(Msqrt, n2);

		bn NMsqrts1 = NMsqrt / s1;
		if (NMsqrts1 >= LIMIT) {
			return{ -1,-1,-1 };
		}

		bn Msqrts2 = Msqrt / s2;
		if (Msqrts2 >= LIMIT) {
			return{ -1,-1,-1 };
		}

		bn n2s2 = n2 / s2;
		bn m2s1 = m2 / s1;

		n3 = NMsqrts1*n2s2;
		m3 = Msqrts2*m2s1;

		if ((n3 <= LIMIT) && (m3 <= LIMIT)) {
			return{ n3,m3,k1 };
		}
		else {
			return{ -1,-1,-1 };
		}
	}
	return{ -2,-2,-2 };			//мы вообще тут не должны находиться.

}

//Функция вычитания двух троек.
static struct Triple RatSub(struct Triple const &Triple1, struct Triple const &Triple2)
{
	bn m1, n1, m2, n2;
	int k1, k2;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	m2 = Triple2.m;
	n2 = Triple2.n;
	n2.neg();
	k2 = Triple2.k;
	return RatSum({ n1,m1,k1 }, { n2,m2,k2 });
}


//Функция умнжения двух троек.
static struct Triple RatMul(struct Triple const &Triple1, struct Triple const &Triple2) {
	bn m1, n1, m2, n2;
	int k1, k2;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	m2 = Triple2.m;
	n2 = Triple2.n;
	k2 = Triple2.k;

	if (k2 < k1) {
		int m1ch, m2ch, n1ch, n2ch, m1bits, m2bits, n1bits, n2bits, LIMITch;
		m1ch = m1.chunks();
		m2ch = m2.chunks();
		n1ch = n1.chunks();
		n2ch = n2.chunks();
		m1bits = m1.bits();
		n1bits = n1.bits();
		m2bits = m2.bits();
		n2bits = n2.bits();
		int LIMITbits = LIMIT_log;
		LIMITch = LIMIT.chunks();
		if ((m2ch - 2)*pow(2, k1 - k2) - n1ch >= LIMITch + 2) {		//предварительная проверка, что числитель не очень большой.
			return{ -1,-1,-1 };
		}

		if ((n2ch - 2)*pow(2, k1 - k2) - m1ch >= LIMITch + 2) {		//предварительная проверка, что знаменатель не очень большой.
			return{ -1,-1,-1 };
		}


		if ((m2bits - 1)*pow(2, k1 - k2) - n1bits >= LIMITbits + 2) {		//предварительная проверка, что числитель не очень большой.
			return{ -1,-1,-1 };
		}

		if ((n2bits - 1)*pow(2, k1 - k2) - m1bits >= LIMITbits + 2) {		//предварительная проверка, что знаменатель не очень большой.
			return{ -1,-1,-1 };
		}

		int k12 = pow(2, k1 - k2);
		bn a, b, n2a, m2b;
		m2.pow_to(k12);
		n2.pow_to(k12);
		a = bn::gcd(m1, n2);
		b = bn::gcd(n1, m2);
		n2a = n2 / a;
		m2b = m2 / b;
		if ((n2a >= LIMIT) || (m2b >= LIMIT)) {
			return{ -1,-1,-1 };
		}

		bn n1b, m1a, n3, m3;
		n1b = n1 / b;
		m1a = m1 / a;
		m3 = m1a*m2b;
		n3 = n1b*n2a;
		if ((n3 >= LIMIT) || (m3 >= LIMIT)) {
			return{ -1,-1,-1 };
		}
		return{ n3, m3, k1 };
	}

	if (k1 < k2) {
		return RatMul(Triple2, Triple1);
	}

	if (k1 == k2) {
		int k3 = k1;
		bn n1n2, m1m2, gcdd, a, b;
		n1n2 = n1*n2;
		m1m2 = m1*m2;
		//printf("%s\n",n1n2.to_string().c_str());
		//printf("%s\n",m1m2.to_string().c_str());
		gcdd = bn::gcd(n1n2, m1m2);
		a = n1n2 / gcdd;
		b = m1m2 / gcdd;
		for (int i = 1; i <= k1; i++) {
			pair <int, bn> pair1;
			pair1 = IsSquare(a);
			if (pair1.first == -1) {
				break;
			}
			pair <int, bn> pair2;
			pair2 = IsSquare(b);
			if (pair2.first == -1) {
				break;
			}
			a = pair1.second;
			b = pair2.second;
			k3--;
		}
		if ((a >= LIMIT) || (b >= LIMIT)) {
			return{ -1, -1, -1 };
		}
		return{ a,b,k3 };
	}
	return{ -2,-2,-2 };		//мы вообще тут не должны находиться.
}

//Функция деления двух троек.
static struct Triple RatDiv(struct Triple const & Triple1, struct Triple const &Triple2) {
	bn m1, n1, m2, n2;
	int k1, k2;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	m2 = Triple2.m;
	n2 = Triple2.n;
	k2 = Triple2.k;

	return RatMul(Triple1, { m2,n2,k2 });
}

//Функция, которая возвращает максимальную степень двойки в числе, но не больше limit. Не оптимально!!
static int Power2InNumber(bn BigNum, int limit) {
	int k = 0;
	for (int i = 1; i <= limit; i++) {
		if (BigNum % 2 == 0) {
			BigNum = BigNum / 2;
			k++;
		}
	}
	return k;
}

//Функция которая проверяет, является ли число степенью
static pair<int, bn> IsPowerGeneral(bn Number, int power) {
	if (power == 1) {
		return{ 1,Number };
	}
	if (power >= LIMIT_log) {
		return{ -1, -1 };
	}
	bn NumberCopy = Number;
	bn SqrtPow = Number.root_to(power);
	bn SqrtCopy = SqrtPow;
	Number.pow_to(power);
	if (Number == NumberCopy) {
		return{ 1, SqrtCopy };
	}
	else {
		return{ -1,-1 };
	}
}

//Функция возведения тройки в тройку.
static struct Triple RatPower(struct Triple const &Triple1, struct Triple const &Triple2) {
	bn m1, n1, m2, n2;
	int k1, k2;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	m2 = Triple2.m;
	n2 = Triple2.n;
	k2 = Triple2.k;
	if (k2 != 0) {
		return{ -1,-1,-1 };
	}
	if ((m1 == 1) && (n1 == 1)) {
		return{ -1,-1,-1 };
	}
	int m2pow2 = Power2InNumber(m2, LIMIT_log_log + NumOfRoots + 1);
	if (m2pow2 - LIMIT_log_log > NumOfRoots) {
		return{ -1, -1, -1 };
	}
	if (m2 % 2 == 0) {		//знаменатель делится на 2, значит числитель нет.
		for (int i = 1; i <= m2pow2; i++) {
			pair<int, bn> t1 = IsSquare(n1);
			pair<int, bn> t2 = IsSquare(m1);
			if ((t1.first == -1) || (t2.first == -1)) {
				k1++;
				if (k1 > NumOfRoots) {
					return{ -1,-1,-1 };
				}
				m2 = m2 / 2;
			}
			else {
				m2 = m2 / 2;
				n1 = t1.second;
				m1 = t2.second;
			}
		}

		//Если мы тут, то поидее знаменатель больше не делится на 2
		int n2int = n2 % 10000;
		int m2int = m2 % 10000;

		pair<int, bn> tmp1 = IsPowerGeneral(n1, m2int);
		if (tmp1.first == -1) {
			return{ -1,-1,-1 };
		}

		pair<int, bn> tmp2 = IsPowerGeneral(m1, m2int);
		if (tmp2.first == -1) {
			return{ -1,-1,-1 };
		}

		bn n3 = tmp1.second;
		bn m3 = tmp2.second;

		if (n2 > LIMIT_log) {
			return{ -1,-1,-1 };
		}
		if ((MaxNumbersForPowers[n2int] < n3) || (MaxNumbersForPowers[n2int] < m3)) {
			return{ -1,-1,-1 };
		}

		n3.pow_to(n2int);
		m3.pow_to(n2int);
		if ((n1 >= LIMIT) || (m1 >= LIMIT)) {
			return{ -1,-1,-1 };
		}
		return{ n3,m3,k1 };
	}
	else {					// знаменатель не делится на 2, может только числитель.
		int k1cp = k1;
		for (int i = 1; i <= k1cp; i++) {
			if (n2 % 2 == 0) {
				n2 = n2 / 2;
				k1--;
			}
		}

		int n2int = n2 % 10000;
		int m2int = m2 % 10000;

		pair<int, bn> tmp1 = IsPowerGeneral(n1, m2int);
		if (tmp1.first == -1) {
			return{ -1,-1,-1 };
		}

		pair<int, bn> tmp2 = IsPowerGeneral(m1, m2int);
		if (tmp2.first == -1) {
			return{ -1,-1,-1 };
		}

		n1 = tmp1.second;
		m1 = tmp2.second;

		if (n2 > LIMIT_log) {
			return{ -1,-1,-1 };
		}
		if ((MaxNumbersForPowers[n2int] < n1) || (MaxNumbersForPowers[n2int] < m1)) {
			return{ -1,-1,-1 };
		}

		n1.pow_to(n2int);
		m1.pow_to(n2int);
		if ((n1 >= LIMIT) || (m1 >= LIMIT)) {
			return{ -1,-1,-1 };
		}
		return{ n1,m1,k1 };
	}

}

static struct Triple RatPower2(struct Triple const &Triple1, struct Triple const &Triple2) {
	int k1, k2;
	const bn m1 = Triple1.m;
	const bn n1 = Triple1.n;
	k1 = Triple1.k;
	const bn m2 = Triple2.m;
	const bn n2 = Triple2.n;
	k2 = Triple2.k;
	return RatPower({ m1,n1,k1 }, { n2,m2,k2 });
}

static Triple RatSqrt(Triple const &Triple1) {
	bn m1, n1;
	int k1;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	if (k1 == 0) {		//В этом случае надо будет проверять, является ли число квадратом.
		pair<int, bn> t1 = IsSquare(n1);
		if (t1.first == -1) {
			return{ n1,m1,1 };
		}

		pair<int, bn> t2 = IsSquare(m1);
		if (t2.first == -1) {
			return{ n1,m1,1 };
		}
		return{ t1.second,t2.second,0 };
	}
	if (k1 < NumOfRoots) {
		return{ n1,m1,k1 + 1 };
	}
	return{ -1,-1,-1 };
}


//Возводит число в квадрат.
static Triple RatSquare(Triple const &Triple1) {
	bn m1, n1;
	int k1;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	if (k1 > 0) {
		return{ n1,m1,k1 - 1 };
	}
	if (k1 == 0) {
		return RatMul(Triple1, Triple1);
	}
	throw "RatSquare with negative";
}


//Вычисляет корень степени Triple1 из числа Triple2.
static Triple RatPowerInverse(Triple Triple1, Triple Triple2) {
	bn m1, n1;
	int k1;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	return RatPower(Triple2, { m1,n1,k1 });
}

//Вычисляет логарифм по основанию Triple1 от числа Triple2 - TO DO
static Triple RatLog(Triple const &Triple1, Triple const &Triple2) {
	//bn m1, n1, m2, n2;
	int k1, k2;
	const bn m1 = Triple1.m;
	const bn n1 = Triple1.n;
	k1 = Triple1.k;
	const bn m2 = Triple2.m;
	const bn n2 = Triple2.n;
	k2 = Triple2.k;
	if ((k1 == 0) && (k2 == 0)) {

		if (((n2 == n1) && (m2 != m1) && (n1 != 1)) || ((n2 != n1) && (m2 == m1) && (m1 != 1))) {
			return{ -1,-1,-1 };
		}

		int s1, s2;

		if (n1 > n2) {
			s1 = -1;
		}
		else if (n1 < n2) {
			s1 = 1;
		}
		else {
			s1 = 0;
		}

		if (m1 > m2) {
			s2 = -1;
		}
		else if (m1 < m2) {
			s2 = 1;
		}
		else {
			s2 = 0;
		}

		if ((m1 == 1) && (n1 == 1)) {
			return{ -1,-1,-1 };
		}

		if (((s1 > 0) && (s2 < 0)) || ((s1 < 0) && (s2 > 0))) {
			return{ -1,-1,-1 };
		}
		if (((s1 < 0) && (s2 == 0)) || ((s2 < 0) && (s1 == 0)) || ((s1 < 0) && (s2 < 0))) {
			Triple Tr1 = RatLog(Triple2, Triple1);
			return{ Tr1.m,Tr1.n,Tr1.k };
		}

		if ((s1 == 0) && (s2 == 0)) {
			return{ 1,1,0 };
		}

		{
			bn tt = n2%n1;
			if (tt != 0) {
				return{ -1,-1,-1 };
			}
		}

		if (m2%m1 != 0) {
			return{ -1,-1,-1 };
		}

		Triple ans = RatLog(Triple1, { n2 / n1,m2 / m1,k2 });

		if (ans.k == -1) {
			return{ -1,-1,-1 };
		}

		return{ ans.n + ans.m,ans.m,0 };
	}
	else if (k1 > k2) {
		Triple ans1 = RatLog({ n1,m1,0 }, { n2,m2,0 });
		if (ans1.k == -1) {
			return{ -1,-1,-1 };
		}
		Triple a2 = { pow(2,k1 - k2),1,0 };
		return RatMul(ans1, a2);
	}
	else {
		Triple ans1 = RatLog({ n1,m1,0 }, { n2,m2,0 });
		if (ans1.k == -1) {
			return{ -1,-1,-1 };
		}
		Triple a2 = { 1, pow(2,k2 - k1),0 };
		return RatMul(ans1, a2);
	}
}



static void GenerateFactorialTable() {
	ArrayOfFactorials[0] = 1;
	ArrayOfFactorials[1] = 1;
	for (int i = 2; i < LIMIT_log; i++) {
		ArrayOfFactorials[i] = ArrayOfFactorials[i - 1] * i;
		if (ArrayOfFactorials[i] >= LIMIT) {
			ArrayOfFactorials[i] = -1;
			break;
		}
	}
}

static Triple RatFac(Triple Triple1) {
	bn m1, n1;
	int k1;
	m1 = Triple1.m;
	n1 = Triple1.n;
	k1 = Triple1.k;
	if ((m1 != 1) || (k1 != 0) || (n1 >= LIMIT_log)) {
		return{ -1,-1,-1 };
	}
	bn n1fac = ArrayOfFactorials[n1%LIMIT_log];
	if (n1fac == -1) {
		return{ -1,-1,-1 };
	}
	else {
		return{ n1fac,1,0 };
	}
}



using triple_func = Triple(*)(const Triple& left, const Triple& right);
static triple_func functions[7] = {
	nullptr,
	RatMul,
	RatSum,
	RatDiv,
	RatSub,
	RatPower,
	RatPower2,
	//RatSqrt,
	//RatFac
};

//Функция, которая записывает в глобальный массив MaxNumberForPowers максимальное число для каждой степени.
static void GeneratePowersLimits() {
	MaxNumbersForPowers[1] = LIMIT;
	for (int i = 2; i <= LIMIT_log; i++) {
		bn LIMITcopy = LIMIT;
		LIMITcopy.root_to(i);
		MaxNumbersForPowers[i] = LIMITcopy;
	}
}

static string TripleToKey(Triple const &k) {
    stringstream s;
    s << "@" << k.n.to_string(16) << "@" << k.m.to_string(16) << "@" << k.k;
    return s.str();
}

//эту функция под внешний хэш
static string FindSolutionUsingSmallSolTriple(Triple const &key, int depp) {
	for (int i = 0; i < depp; i++) {
        string r;
        if (smallSolTriple[i].find(TripleToKey(key), r)) return r;
	}
	return "@@@";
}


// Функция для умного обратного поиска.
static string FindSmartSolution(Triple const &Triple1, int depthLarge) {
	//printf("New FindSmartSolution iteration is startnig with %s, %s, %d; depthLarge = %d\n", Triple1.n.to_string().c_str(), Triple1.m.to_string().c_str(), Triple1.k, depthLarge);

	bn Ndigits = 10;
	Ndigits.pow_to(depthLarge);
	Ndigits = Ndigits - 1;
	Ndigits /= 9;
	Ndigits *= digit;

	if ((Triple1.n == Ndigits) && (Triple1.m == 1) && (Triple1.k == 0)) {
		return Ndigits.to_string();
	}

	for (int depp = 1; depp <= depthLarge - depp; depp++) {
		//printf("depp = %d\n", depp);
        string q;
        for (auto pq = smallSolTriple[depp - 1].first(q);
            pq.first != pht::NOT_FOUND;  pq = smallSolTriple[depp - 1].next(pq, q)) {
            //printf("pq=%llu\n", pq);
            Triple NumToProcess(q);
            //printf("pq=(%llu,%llu) depp=%d q=%s\n", pq.first, pq.second, depp, TripleToString(NumToProcess).c_str());
			Triple Numbers;
			string ResultFix, ResultOut, Result;
			ResultFix = FindSolutionUsingSmallSolTriple(NumToProcess, depp);
			//printf("%s\n", ResultFix.c_str());

			// 1 out of 10 possible options
			Numbers = RatSub(Triple1, NumToProcess);

			if (Numbers.k != -1) {
				if (depp >= depthLarge - depth) {
					string Try;
					Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
					if (Try != "@@@") {
						Result = Try;
						ResultOut = "(" + Result + ")+(" + ResultFix + ")";
						printf("CASE11\n");
						return ResultOut;
					}
				}

				if (depp < depthLarge - depth) {
					Result = FindSmartSolution(Numbers, depthLarge - depp);
					if (Result != "@@@@") {
						ResultOut = "(" + Result + ")+(" + ResultFix + ")";
						printf("CASE12\n");
						return ResultOut;
					}
				}
			}

			// 2 out of 10 possible options
			Numbers = RatSub(NumToProcess, Triple1);

			if (Numbers.k != -1) {
				if (depp >= depthLarge - depth) {
					string Try;
					Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
					if (Try != "@@@") {
						Result = Try;
						ResultOut = "(" + ResultFix + ")-(" + Result + ")";
						printf("CASE21\n");
						return ResultOut;
					}
				}

				if (depp < depthLarge - depth) {
					Result = FindSmartSolution(Numbers, depthLarge - depp);
					if (Result != "@@@@") {
						ResultOut = "(" + ResultFix + ")-(" + Result + ")";
						printf("CASE22\n");
						return ResultOut;
					}
				}
			}

			// 3 out of 10 possible options
			//printf("Option3\n");
			Numbers = RatSum(NumToProcess, Triple1);

			if (Numbers.k != -1) {
				if (depp >= depthLarge - depth) {
					string Try;
					Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
					if (Try != "@@@") {
						Result = Try;
						ResultOut = "(" + Result + ")-(" + ResultFix + ")";
						printf("CASE31\n");
						return ResultOut;
					}
				}

				if (depp < depthLarge - depth) {
					Result = FindSmartSolution(Numbers, depthLarge - depp);
					if (Result != "@@@@") {
						ResultOut = "(" + Result + ")-(" + ResultFix + ")";
						printf("CASE32\n");
						return ResultOut;
					}
				}
			}

			// 4 out of 10 possible options
			Numbers = RatDiv(NumToProcess, Triple1);

			if (Numbers.k != -1) {
				if (depp >= depthLarge - depth) {
					string Try;
					Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
					if (Try != "@@@") {
						Result = Try;
						ResultOut = "\\frac{" + ResultFix + "}{" + Result + "}";
						printf("CASE41\n");
						return ResultOut;
					}
				}

				if (depp < depthLarge - depth) {
					Result = FindSmartSolution(Numbers, depthLarge - depp);
					if (Result != "@@@@") {
						ResultOut = "\\frac{" + ResultFix + "}{" + Result + "}";
						printf("CASE42\n");
						return ResultOut;
					}
				}
			}

			// 5 out of 10 possible options
			Numbers = RatMul(NumToProcess, Triple1);

			if (Numbers.k != -1) {
				if (depp >= depthLarge - depth) {
					string Try;
					Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
					if (Try != "@@@") {
						Result = Try;
						ResultOut = "\\frac{" + Result + "}{" + ResultFix + "}";
						printf("CASE51\n");
						return ResultOut;
					}
				}

				if (depp < depthLarge - depth) {
					Result = FindSmartSolution(Numbers, depthLarge - depp);
					if (Result != "@@@@") {
						ResultOut = "\\frac{" + Result + "}{" + ResultFix + "}";
						printf("CASE52\n");
						return ResultOut;
					}
				}
			}

			// 6 out of 10 possible options
			Numbers = RatDiv(Triple1, NumToProcess);

			if (Numbers.k != -1) {
				if (depp >= depthLarge - depth) {
					string Try;
					Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
					if (Try != "@@@") {
						Result = Try;
						ResultOut = "(" + Result + ")*(" + ResultFix + ")";
						printf("CASE61\n");
						return ResultOut;
					}
				}

				if (depp < depthLarge - depth) {
					Result = FindSmartSolution(Numbers, depthLarge - depp);
					if (Result != "@@@@") {
						ResultOut = "(" + Result + ")*(" + ResultFix + ")";
						printf("CASE62\n");
						return ResultOut;
					}
				}
			}

			if (EVALUATE_LOGS == 1) {

				// 7 out of 10 possible options
				Numbers = RatLog(NumToProcess, Triple1);
				if (Numbers.k != -1) {
					if (depp >= depthLarge - depth) {
						string Try;
						Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
						if (Try != "@@@") {
							Result = Try;
							ResultOut = "(" + ResultFix + ")^{" + Result + "}";
							printf("CASE71\n");
							return ResultOut;
						}
					}

					if (depp < depthLarge - depth) {
						Result = FindSmartSolution(Numbers, depthLarge - depp);
						if (Result != "@@@@") {
							ResultOut = "(" + ResultFix + ")^{" + Result + "}";
							printf("CASE72\n");
							return ResultOut;
						}
					}
				}

				// 8 out of 10 possible options
				Triple Triple1Inv = { Triple1.m,Triple1.n,Triple1.k };
				Numbers = RatLog(NumToProcess, Triple1Inv);

				if (Numbers.k != -1) {
					if (depp >= depthLarge - depth) {
						string Try;
						Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
						if (Try != "@@@") {
							Result = Try;
							ResultOut = "(" + ResultFix + ")^{-(" + Result + ")}";
							printf("CASE81\n");
							return ResultOut;
						}
					}

					if (depp < depthLarge - depth) {
						Result = FindSmartSolution(Numbers, depthLarge - depp);
						if (Result != "@@@@") {
							ResultOut = "(" + ResultFix + ")^{-(" + Result + ")}";
							printf("CASE82\n");
							return ResultOut;
						}
					}
				}

				// 9 out of 10 possible options
				Numbers = RatPowerInverse(NumToProcess, Triple1);

				if (Numbers.k != -1) {
					if (depp >= depthLarge - depth) {
						string Try;
						Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
						if (Try != "@@@") {
							Result = Try;
							ResultOut = "(" + Result + ")^{" + ResultFix + "}";
							printf("CASE91\n");
							return ResultOut;
						}
					}

					if (depp < depthLarge - depth) {
						Result = FindSmartSolution(Numbers, depthLarge - depp);
						if (Result != "@@@@") {
							ResultOut = "(" + Result + ")^{" + ResultFix + "}";
							printf("CASE92\n");
							return ResultOut;
						}
					}
				}

				// 10 out of 10 possible options
				Numbers = RatPowerInverse(NumToProcess, Triple1Inv);

				if (Numbers.k != -1) {
					if (depp >= depthLarge - depth) {
						string Try;
						Try = FindSolutionUsingSmallSolTriple(Numbers, depthLarge - depp);
						if (Try != "@@@") {
							Result = Try;
							ResultOut = "(" + Result + ")^{-(" + ResultFix + ")}";
							printf("CASE01\n");
							return ResultOut;
						}
					}

					if (depp < depthLarge - depth) {
						Result = FindSmartSolution(Numbers, depthLarge - depp);
						if (Result != "@@@@") {
							ResultOut = "(" + Result + ")^{-(" + ResultFix + ")}";
							printf("CASE02\n");
							return ResultOut;
						}
					}
				}

			}

		}
	}

	if (DO_REVERSE_ROOTS == 1) {
		Triple Triple2 = RatSquare(Triple1);
		if (Triple2.k != -1) {
			string sol = FindSmartSolution(Triple2, depthLarge);
			if (sol != "@@@@") {
				return "\\sqrt[" + sol + "]";
			}
			else {
				return "@@@@";
			}
		}
		else {
			return "@@@@";
		}
	}

	return "@@@@";
}

static int FindNumberOfDigits(string const &str) {
	int iter = 0;

	for (int i = 0; i < str.size(); i++) {
		iter += str[i] == '0' + digit;
	}
	return iter;
}


//Функция, которая делает первый пробег по массиву, и находит обычные решения вплоть до LIM (Которое мы возьмём 10*BigN)
static void FillArrayOfSolutionsStep1(int LIM) {
	int NumOfMinusOnes = 0;
	for (int i = 1; i <= LIM; i++) {
		string Option1 = FindSolutionUsingSmallSolTriple({ i,1,0 }, depth);
		if (Option1 != "@@@") {
			ArrayOfSolutions[i] = Option1.c_str();
			ArrayOfNumDigits[i] = FindNumberOfDigits(Option1);
			//printf("%s\n", ArrayOfSolutions[i].c_str());
		}
	}
	for (int i = 1; i <= BigN; i++) {
		if (ArrayOfNumDigits[i] == -1) {
			NumOfMinusOnes++;
		}
	}
	printf("Number of minus ones is %d\n", NumOfMinusOnes);
}


//Функция для поиска тупого решения.
static void FindStupidSolutionStep2(int LIM, int depStupid) {

	int NumOfMinusOnes = 0;

	//1 вариант
	for (int i = 1; i <= LIM; i++) {
		if ((ArrayOfNumDigits[i] == -1) && (i + digit <= BigN * 10) && (ArrayOfNumDigits[i + digit] == depStupid - 1)) {
			ArrayOfNumDigits[i] = depStupid;
			ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i + digit] + ")-(" + to_string(digit) + ")";
		}
	}

	//2 вариант
	for (int i = 1; i <= LIM; i++) {
		if ((ArrayOfNumDigits[i] == -1) && (i - digit <= BigN * 10) && (i - digit > 0) && (ArrayOfNumDigits[i - digit] == depStupid - 1)) {
			ArrayOfNumDigits[i] = depStupid;
			ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i - digit] + ")+(" + to_string(digit) + ")";
		}
	}

	int digitFac = ArrayOfFactorials[digit] % 10000000;

	//3 вариант
	for (int i = 1; i <= LIM; i++) {
		if ((ArrayOfNumDigits[i] == -1) && (i + digitFac <= BigN * 10) && (ArrayOfNumDigits[i + digitFac] == depStupid - 1)) {
			ArrayOfNumDigits[i] = depStupid;
			ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i + digitFac] + ")-(" + to_string(digit) + "!)";
		}
	}

	//4 вариант
	for (int i = 1; i <= LIM; i++) {
		if ((ArrayOfNumDigits[i] == -1) && (i - digitFac <= BigN * 10) && (i - digitFac > 0) && (ArrayOfNumDigits[i - digitFac] == depStupid - 1)) {
			ArrayOfNumDigits[i] = depStupid;
			ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i - digitFac] + ")+(" + to_string(digit) + "!)";
		}
	}

	//5 вариант
	for (int i = 1; i <= LIM; i++) {
		if ((ArrayOfNumDigits[i] == -1) && (digitFac - i <= BigN * 10) && (digitFac - i > 0) && (ArrayOfNumDigits[digitFac - i] == depStupid - 1)) {
			ArrayOfNumDigits[i] = depStupid;
			ArrayOfSolutions[i] = "(" + to_string(digit) + "!)-(" + ArrayOfSolutions[digitFac - i] + ")";
		}
	}

	//6 вариант
	for (int i = 1; i <= LIM; i++) {
		if ((ArrayOfNumDigits[i] == -1) && (i*digit <= BigN * 10) && (ArrayOfNumDigits[i*digit] == depStupid - 1)) {
			ArrayOfNumDigits[i] = depStupid;
			ArrayOfSolutions[i] = "\\frac{" + ArrayOfSolutions[i*digit] + "}{" + to_string(digit) + "}";
		}
	}

	//7 вариант
	for (int i = 1; i <= LIM; i++) {
		long long int T = (long long)i * (long long)digitFac;
		if ((ArrayOfNumDigits[i] == -1) && (T <= BigN * 10) && (ArrayOfNumDigits[i*digitFac] == depStupid - 1)) {
			ArrayOfNumDigits[i] = depStupid;
			ArrayOfSolutions[i] = "\\frac{" + ArrayOfSolutions[i*digitFac] + "}{" + to_string(digit) + "!}";
		}
	}

	if ((digit == 4) || (digit == 9)) {
		int digitSq = 1;
		if (digit == 4) {
			digitSq = 2;
		}

		if (digit == 9) {
			digitSq = 3;
		}

		//8 вариант
		for (int i = 1; i <= LIM; i++) {
			if ((ArrayOfNumDigits[i] == -1) && (i + digitSq <= BigN * 10) && (ArrayOfNumDigits[i + digitSq] == depStupid - 1)) {
				ArrayOfNumDigits[i] = depStupid;
				ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i + digitSq] + ")-\\sqrt[" + to_string(digit) + "]";
			}
		}

		//9 вариант
		for (int i = 1; i <= LIM; i++) {
			if ((ArrayOfNumDigits[i] == -1) && (i - digitSq <= BigN * 10) && (i - digitSq > 0) && (ArrayOfNumDigits[i - digitSq] == depStupid - 1)) {
				ArrayOfNumDigits[i] = depStupid;
				ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i - digitSq] + ")+\\sqrt[" + to_string(digit) + "]";
			}
		}

		//10 вариант
		for (int i = 1; i <= LIM; i++) {
			if ((ArrayOfNumDigits[i] == -1) && (i*digitSq <= BigN * 10) && (ArrayOfNumDigits[i*digitSq] == depStupid - 1)) {
				ArrayOfNumDigits[i] = depStupid;
				ArrayOfSolutions[i] = "\\frac{" + ArrayOfSolutions[i*digitSq] + "}{\\sqrt[" + to_string(digit) + "]}";
			}
		}
	}

	if (digit == 3) {
		//11 вариант
		for (int i = 1; i <= LIM; i++) {
			if ((ArrayOfNumDigits[i] == -1) && (i - 720 <= BigN * 10) && (i - 720 > 0) && (ArrayOfNumDigits[i - 720] == depStupid - 1)) {
				ArrayOfNumDigits[i] = depStupid;
				ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i - 720] + ")+((3!)!)";
			}
		}

		//12 вариант
		for (int i = 1; i <= LIM; i++) {
			if ((ArrayOfNumDigits[i] == -1) && (i + 720 <= BigN * 10) && (ArrayOfNumDigits[i + 720] == depStupid - 1)) {
				ArrayOfNumDigits[i] = depStupid;
				ArrayOfSolutions[i] = "(" + ArrayOfSolutions[i + 720] + ")-((3!)!)";
			}
		}
	}

	for (int i = 1; i <= BigN; i++) {
		if (ArrayOfNumDigits[i] == -1) {
			NumOfMinusOnes++;
		}
	}
	printf("Number of minus ones is %d\n", NumOfMinusOnes);
}

#ifndef _WIN64
using tharg = struct {
    queue<int> *q; 
    mutex *mut;
    int    depU;
}; 

static void * numberEater(void *_a) {
    tharg *a = (tharg *)_a;
    printf("Hi from new thread\n");
    for (;;) {
        a->mut->lock();
        if (a->q->empty()) {
            a->mut->unlock();
            return NULL;
        }
        auto i = a->q->front(); a->q->pop();
        a->mut->unlock();
        if (ArrayOfNumDigits[i] == -1) {
            printf("Eater: try %d\n", i);
            try {
                string Option2 = FindSmartSolution({ i,1,0 }, a->depU);
                if (Option2 != "@@@@") {
                    ArrayOfSolutions[i] = Option2;
                    ArrayOfNumDigits[i] = FindNumberOfDigits(Option2);
                    printf("%d = %s\n", i, Option2.c_str());
                }
            } catch (const char *s) {
                printf("Caught %s\n", s);
                throw;
            }
        }
    }
    return NULL;
}

static void FindSmartSolutionsStep3(int LIM, int depU) {
	int NumOfMinusOnes = 0;
    queue<int> bigq;
    mutex mut;
    for (int i = 1; i <= LIM; i++) {
        bigq.push(i);
    }
    pthread_t pool[NUM_THREADS];
    tharg     args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_attr_t st;
        pthread_attr_init(&st);
        pthread_attr_setstacksize(&st, 32*1024*1024);
        args[i].q = &bigq;
        args[i].mut = &mut;
        args[i].depU = depU;
        int code = pthread_create(pool+i, &st, numberEater, args+i);
        if (code != 0) {
            printf("thread failed: errno=%d (%s)\n", code, strerror(code));
        }
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(pool[i], NULL);
    }
	for (int i = 1; i <= BigN; i++) {
		if (ArrayOfNumDigits[i] == -1) {
			NumOfMinusOnes++;
		}
	}
	printf("Number of minus ones is %d\n", NumOfMinusOnes);
}


#else

//using tharg = tuple<queue<int> &q, mutex 
static void numberEater(queue<int> *q, mutex *mut, int depU) {
	for (;;) {
		mut->lock();
		if (q->empty()) {
			mut->unlock();
			return;
		}
		auto i = q->front(); q->pop();
		mut->unlock();
		if (ArrayOfNumDigits[i] == -1) {
            printf("Eater: try %d\n", i);
			string Option2 = FindSmartSolution({ i,1,0 }, depU);
			if (Option2 != "@@@@") {
				ArrayOfSolutions[i] = Option2;
				ArrayOfNumDigits[i] = FindNumberOfDigits(Option2);
				printf("%d = %s\n", i, Option2.c_str());
			}
		}
	}
}

static void FindSmartSolutionsStep3(int LIM, int depU) {
	int NumOfMinusOnes = 0;
	queue<int> bigq;
	mutex mut;
	for (int i = 1; i <= LIM; i++) {
		bigq.push(i);
	}
	thread pool[NUM_THREADS];
	for (int i = 0; i < NUM_THREADS; i++) {
		pool[i] = thread(numberEater, &bigq, &mut, depU);
	}
	for (int i = 0; i < NUM_THREADS; i++) {
		pool[i].join();
	}
	for (int i = 1; i <= BigN; i++) {
		if (ArrayOfNumDigits[i] == -1) {
			NumOfMinusOnes++;
		}
	}
	printf("Number of minus ones is %d\n", NumOfMinusOnes);
}
#endif


int parse5(char *s, int sizedata, char **s0, char **s1, char **s2, char **s3, char **s4) {
    char *e = s + sizedata;
    int ret = 0;
    *s0 = s;
    ret++;
 
    s = strchr(s, ' ');
    if (s == NULL || s >= e) return ret;
    *s++ = 0;
    *s1 = s;
    ret++; 

    s = strchr(s, ' ');
    if (s == NULL || s >= e) return ret;
    *s++ = 0;
    *s2 = s;
    ret++; 

    s = strchr(s, ' ');
    if (s == NULL || s >= e) return ret;
    *s++ = 0;
    *s3 = s;
    ret++; 

    s = strchr(s, ' ');
    if (s == NULL || s >= e) return ret;
    *s++ = 0;
    *s4 = s;
    ret++;
    char *q = strchr(s,'\n');
    if (q != NULL) *q = 0;
    return ret;
}

//считывает один файл и создаёт из него map из Triple и String
static map<Triple, string> ReadOneFileBN_String(string const &sOp) {
	map<Triple, string> v;
	FILE *in = fopen(sOp.c_str(), "r");
    if (in == NULL) {
        perror(sOp.c_str());
        abort();
    }
    
    char line[20000];
	while (fgets(line, sizeof line, in) != NULL) {
        char *s[5];
        if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
		bn s0(s[0]), s1(s[1]);
		int s2 = atoi(s[2]);
		v[{ s0, s1, s2 }] = s[4];				//Проверить!!!!!!!
	}
	fprintf(stderr, "%s: total %zu lines\n", sOp.c_str(), v.size());
	fclose(in);
	return v;
}



//Считать один файл в map из String и String, сжирает не очень много памяти, в идеале надо сделать 16ричным.
static map<string, string>  ReadOneFileStrings(string sOp) {
	map<string, string> v;
	FILE *in = fopen(sOp.c_str(), "r");
    if (in == NULL) {
        perror(sOp.c_str());
        abort();
    }
    
	char line[20000];
    char *s[5];
	while (fgets(line, sizeof line, in) != NULL) {
        if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
		v[(string)s[0] + " " + s[1] + " " + s[2]] =
			(string)s[3] + " " + s[4];
		if (v.size() % 1000 == 0) {
			fprintf(stderr, "%s: %zu       \r", sOp.c_str(), v.size());
			fflush(stderr);
		}
	}
	fprintf(stderr, "%s: total %zu lines\n", sOp.c_str(), v.size());
	fclose(in);
	return v;
}





//Функция MadeBlocks по двум именам файлов и по операции генерирует все возможные пары решений и возвращает только уникальные по ключу и пишет всё в файл.
static void MadeBlocks(string const &FileName1, string const &FileName2, string const &FileNameOut,
	int dp, int operation) {
	string t1, t2, t3;
	if (operation == 1) {
		t1 = " (";
		t2 = ")*(";
		t3 = ")";
	}
	else if (operation == 2) {
		t1 = " (";
		t2 = ")+(";
		t3 = ")";
	}
	else if (operation == 3) {
		t1 = " \\frac{";
		t2 = "}{";
		t3 = "}";
	}
	else if (operation == 4) {
		t1 = " (";
		t2 = ")-(";
		t3 = ")";
	}
	else if (operation == 5) {
		t1 = " (";
		t2 = ")^{";
		t3 = "}";
	}
	else if (operation == 6) {
		t1 = " (";
		t2 = ")^{-(";
		t3 = ")}";
	}
	auto v1 = ReadOneFileBN_String(FileName1);
	auto v2 = ReadOneFileBN_String(FileName2);
	map<string, string> vOut;
	for (auto const &q1 : v1) {
		for (auto const &q2 : v2) {
			Triple Tr1 = q1.first;
			Triple Tr2 = q2.first;
			string sol1 = q1.second;
			string sol2 = q2.second;
			string dpStr = to_string(dp);
			//Тут в зависимости от оператора нужно делать разные вещи, далее пример для суммы:
			Triple TrOut = functions[operation](Tr1, Tr2);
			if (TrOut.k != -1) {
				string tmpOut;
				tmpOut = dpStr + t1 + sol1 + t2 + sol2 + t3;
				vOut[TripleToString(TrOut)] = tmpOut;				//Spend quiet a lot of time
			}
		}
	}
	//Теперь нужно записать vOut в файл.
	FILE *out = fopen(FileNameOut.c_str(), "w");
    if (out == NULL) {
        perror(FileNameOut.c_str());
        abort();
    }
    
	for (auto const &qOut : vOut) {
		fprintf(out, "%s %s\n", qOut.first.c_str(), qOut.second.c_str());
	}
	fclose(out);
}

//Функция, которая на вход получает вектор Parts: количества файлов глубины 1, 2, ..., dp - 1. На выходе генерирует все новые решения
//Протестить
static void IterationToFIle(vector<int> const &Parts, int dp) {
	int sss = 1;
	for (int t = 1; t <= dp / 2; t++) {
		for (int i = 1; i <= Parts[t - 1]; i++) {
			for (int j = 1; j <= Parts[dp - t - 1]; j++) {
				string file1 = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(t) + "Part" + to_string(i);
				string file2 = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(dp - t) + "Part" + to_string(j);
				string fileTmp = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(dp) + "Part";
				string fileOut = fileTmp + to_string(sss);

				printf("Current new file number is %d, processing file %s and file %s\n", sss, file1.c_str(), file2.c_str());
				MadeBlocks(file1, file2, fileOut, dp, 1);
				sss++;

				fileOut = fileTmp + to_string(sss);
				printf("Current new file number is %d, processing file %s and file %s\n", sss, file1.c_str(), file2.c_str());
				MadeBlocks(file1, file2, fileOut, dp, 2);
				sss++;
			}
		}
	}

	for (int t = 1; t <= dp - 1; t++) {
		for (int i = 1; i <= Parts[t - 1]; i++) {
			for (int j = 1; j <= Parts[dp - t - 1]; j++) {
				string file1 = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(t) + "Part" + to_string(i);
				string file2 = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(dp - t) + "Part" + to_string(j);
				string fileTmp = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(dp) + "Part";
				string fileOut = fileTmp + to_string(sss);

				printf("Current new file number is %d, processing file %s and file %s\n", sss, file1.c_str(), file2.c_str());
				MadeBlocks(file1, file2, fileOut, dp, 3);
				sss++;

				fileOut = fileTmp + to_string(sss);
				printf("Current new file number is %d, processing file %s and file %s\n", sss, file1.c_str(), file2.c_str());
				MadeBlocks(file1, file2, fileOut, dp, 4);
				sss++;

				fileOut = fileTmp + to_string(sss);
				printf("Current new file number is %d, processing file %s and file %s\n", sss, file1.c_str(), file2.c_str());
				MadeBlocks(file1, file2, fileOut, dp, 5);
				sss++;

				fileOut = fileTmp + to_string(sss);
				printf("Current new file number is %d, processing file %s and file %s\n", sss, file1.c_str(), file2.c_str());
				MadeBlocks(file1, file2, fileOut, dp, 6);
				sss++;
			}
		}
	}
}

//Функция, которая на вход получает имена файлов и рзабивает их на 81 файл - по первым цифрам.
static void SplitBigFiles(vector<string> const &Names, vector<int> const &DeleteIndex) {
	FILE* out[9][9];
	for (int i = 0; i <= 8; i++) {
		for (int j = 0; j <= 8; j++) {
			string Name = "Digit" + to_string(digit) + "/SplittedFirst" + to_string(i + 1) + "Second" + to_string(j + 1);
			out[i][j] = fopen(Name.c_str(), "w");
            if (out[i][j] == NULL) {
                perror(Name.c_str());
                abort();
            }
            

		}
	}

	for (int i = 0; i < Names.size(); i++) {
		string tmp = "Digit" + to_string(digit) + "/" + Names[i];
		FILE *in = fopen(tmp.c_str(), "r");
        if (in == NULL) {
            perror(tmp.c_str());
            abort();
        }
        
    	char line[20000];
        char *s[5];
    	while (fgets(line, sizeof line, in) != NULL) {
            if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
			int dig1 = s[0][0] - '0';
			int dig2 = s[1][0] - '0';
			string tt = (string)s[0] + " " + s[1] + " " + s[2] + " " + s[3] + " " + s[4];
			fprintf(out[dig1 - 1][dig2 - 1], "%s\n", tt.c_str());
		}
		fclose(in);
		if (DeleteIndex[i] == 1) {
			int code;
			do {
				code = remove(tmp.c_str());
			} while (code == -1);

		}
	}


	for (int i = 0; i <= 8; i++) {
		for (int j = 0; j <= 8; j++) {
			string Name = "Digit" + to_string(digit) + "/SplittedFirst" + to_string(i + 1) + "Second" + to_string(j + 1);
			fclose(out[i][j]);
		}
	}
}

//Функция для дальнейшего разбиения файла - на 121 файл.
static void SplitBigFile2(string const &Name) {
	FILE* out[11][11];
	for (int i = 0; i <= 10; i++) {
		for (int j = 0; j <= 10; j++) {
			string NameT = "Digit" + to_string(digit) + "/" + Name.c_str() + "SplittedFirst" + to_string(i + 1) + "Second" + to_string(j + 1);
			out[i][j] = fopen(NameT.c_str(), "w");
            if (out[i][j] == NULL) {
                perror(NameT.c_str());
                abort();
            }
		}
	}

	string tmp = "Digit" + to_string(digit) + "/" + Name;
	FILE *in = fopen(tmp.c_str(), "r");
    if (in == NULL) {
        perror(tmp.c_str());
        abort();
    }
    
	char line[20000];
    char *s[5];
	while (fgets(line, sizeof line, in) != NULL) {
        if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
		int dig1, dig2;

		if (s[0][1] == '\0') {
			dig1 = 0;
		}
		else {
			dig1 = s[0][1] - '0' + 1;
		}

		if (s[1][1] == '\0') {
			dig2 = 0;
		}
		else {
			dig2 = s[1][1] - '0' + 1;
		}

		string tt = (string)s[0] + " " + s[1] + " " + s[2] + " " + s[3] + " " + s[4];
		fprintf(out[dig1][dig2], "%s\n", tt.c_str());
	}
	fclose(in);

	int code;
	do {
		code = remove(tmp.c_str());
	} while (code == -1);


	for (int i = 0; i <= 10; i++) {
		for (int j = 0; j <= 10; j++) {
			fclose(out[i][j]);
		}
	}
}






static void SaveSolutions(string SolutionsFile1, string SolutionsFile2) {
	FILE *out1 = fopen(SolutionsFile1.c_str(), "w");
    if (out1 == NULL) {
        perror(SolutionsFile1.c_str());
        abort();
    }
    
	FILE *out2 = fopen(SolutionsFile2.c_str(), "w");
    if (out2 == NULL) {
        perror(SolutionsFile2.c_str());
        abort();
    }
    
	for (int i = 1; i <= 10 * BigN; i++) {
		fprintf(out1, "%s\n", ArrayOfSolutions[i].c_str());
		fprintf(out2, "%d\n", ArrayOfNumDigits[i]);
	}
	fclose(out1);
	fclose(out2);
}




//Функция, которая на вход получает строку, и на выход возвращает все возможные строки с факториалами и корнями. (вроде работает)
//Переделать под новый вид
static map<string, string> GererateFacRoot(string const &str, int dp) {
	map <string, string> MapStr;
	map<Triple, pair<int, string>> MapTriple;
	char s[5][10000];
	sscanf(str.c_str(), "%s%s%s%s%s\n", s[0], s[1], s[2], s[3], s[4]);
	bn s0(s[0]), s1(s[1]);
	int s2 = atoi(s[2]);
	int s3 = atoi(s[3]);
	string s4 = s[4];
	MapTriple[{s0, s1, s2}] = { s3,s4 };
	if ((s0 == 1) && (s1 == 1)) {
		for (auto const &q1 : MapTriple) {
			MapStr[q1.first.n.to_string() + " " + q1.first.m.to_string() + " " + to_string(q1.first.k)] =
			{ to_string(q1.second.first) + " " + q1.second.second };
		}
		return MapStr;
	}
	else if (s2 > 0) {
		string tmp = s4;
		for (int i = s2 + 1; i <= NumOfRoots; i++) {
			tmp = "\\sqrt{" + tmp + "}";
			MapTriple[{s[0], s[1], i}] = { dp, tmp };
		}

		for (auto const &q1 : MapTriple) {
			MapStr[q1.first.n.to_string() + " " + q1.first.m.to_string() + " " + to_string(q1.first.k)] =
			{ to_string(q1.second.first) + " " + q1.second.second };
		}

		return MapStr;
	}
	else {		//s2 = 0
				// Сначала посчитаем все корни, чтобы получились целые числа.
		Triple Tr1 = { s0,s1,s2 };
		string tmp;
		tmp = s4;
		for (int i = 0; i <= 1000; i++) {
			Triple Tr2 = RatSqrt(Tr1);
			if (Tr2.k == 0) {
				tmp = "\\sqrt{" + tmp + "}";
				MapTriple[Tr2] = { dp, tmp };
				Tr1 = Tr2;
			}
			else {
				break;
			}
		}

		//Triple LastPut = Tr1;
		//string LastPutStr = tmp;

		// Теперь считаем факториалы
		auto MapTriple2 = MapTriple;
		for (int i = 1; i <= NumOfRoots; i++) {
			tmp = "\\sqrt{" + tmp + "}";
			MapTriple2[{Tr1.n, Tr1.m, i}] = { dp, tmp };
		}
		for (auto const &q1 : MapTriple) {
			Triple Tr1 = q1.first;
			string tmp2 = q1.second.second;
			for (int j = 0; j < 2; j++) {				//почему 2? типо 3, 6, 720, много?
				Triple Tr2 = RatFac(Tr1);
				if ((Tr2.k == -1) || (Tr1.n == 2) || (Tr1.n == 1)) {
					break;
				}
				else {
					tmp2 = "(" + tmp2 + ")!";
					MapTriple2[Tr2] = { dp, tmp2 };
					string tmp3 = tmp2;
					for (int i = 1; i <= NumOfRoots; i++) {
						tmp3 = "\\sqrt{" + tmp3 + "}";
						MapTriple2[{Tr2.n, Tr2.m, i}] = { dp,tmp3 };
					}
					Tr1 = Tr2;
				}
			}
		}

		for (auto const &q1 : MapTriple2) {
			MapStr[q1.first.n.to_string() + " " + q1.first.m.to_string() + " " + 
                to_string(q1.first.k)] =
			{ to_string(q1.second.first) + " " + q1.second.second };
		}
		return MapStr;

	}
}

//Открывает файл, делает с каждой строчкой GererateFacRoot и пишет в новый файл, удаляет старый файл.
static void GenerateFacRootFile(string Name, string fileOut, int dep) {
	auto tt = ReadOneFileStrings(Name);
	FILE *out = fopen(fileOut.c_str(), "w");
    if (out == NULL) {
        perror(fileOut.c_str());
        abort();
    }
    

	for (auto const &q1 : tt) {
		auto tmpstr = GererateFacRoot(q1.first + " " + q1.second, dep);
		for (auto const &q2 : tmpstr) {
			fprintf(out, "%s %s\n", q2.first.c_str(), q2.second.c_str());
		}
	}
	fclose(out);

	int code;
	do {
		code = remove(Name.c_str());
	} while (code == -1);

}


//Открывает файл, убирает в нём дупликаты и пишет в новый файл, удаляет старый файл.
static void SortFile(string Name, string fileOut) {
	auto tt = ReadOneFileStrings(Name);
	FILE *out = fopen(fileOut.c_str(), "w");
    if (out == NULL) {
        perror(fileOut.c_str());
        abort();
    }
    

	for (auto const &q1 : tt) {
		//printf("%s\n", q1.first.c_str());
		fprintf(out, "%s %s\n", q1.first.c_str(), q1.second.c_str());
	}
	fclose(out);
	int code;
	do {
		code = remove(Name.c_str());
	} while (code == -1);
}

//Функция, которая чистит файл от дупликатов старых глубин. (Пока работает в 2 прохода, не оптимально?)
static void RemoveDuplicatesAllDepth(string Name, string fileOut, int dep) {
	//Надо, чтобы при попытке добавления второй части строчки, которая начинается с <=dep -1 она не добавлялась и удалялась 
	//и основная строчка.

	map<string, string> vvv;
	FILE *in = fopen(Name.c_str(), "r");
    if (in == NULL) {
        perror(Name.c_str());
        abort();
    }
    
	FILE *out = fopen(fileOut.c_str(), "w");
    if (out == NULL) {
        perror(fileOut.c_str());
        abort();
    }
    
	char line[20000];
    char *s[5];
	while (fgets(line, sizeof line, in) != NULL) {
        if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
		string k1 = (string)s[0] + " " + s[1] + " " + s[2];
		string k2 = (string)s[3] + " " + s[4];
		int s3 = atoi(s[3]);
		auto r = vvv.find(k1);
		if ((r != vvv.end()) && (s3 <= dep - 1)) {		//ключ нашёлся, затираем старый, чтобы потом больше не добавлять
			vvv[k1] = k2;
		}
		if (r == vvv.end()) {		//ключ не нашёлся, добавить
			vvv[k1] = k2;
		}
	}

	auto vvv2 = vvv;

	for (auto const &q1 : vvv) {
		string tmp = q1.second;
		char s1[10000];
		sscanf(tmp.c_str(), "%s", s1);
		if (atoi(s1) < dep) {
			vvv2.erase(q1.first);
		}
	}


	for (auto const &q1 : vvv2) {
		fprintf(out, "%s %s\n", q1.first.c_str(), q1.second.c_str());
	}

	fclose(in);
	fclose(out);
	int code;
	do {
		code = remove(Name.c_str());
	} while (code == -1);

}

static void MergeFiles(vector<string> const &FileNames, string const &FileNameOut) {
	FILE *out = fopen(FileNameOut.c_str(), "w");
    if (out == NULL) {
        perror(FileNameOut.c_str());
        abort();
    }
    
	for (int i = 0; i < FileNames.size(); i++) {
		string tmp = "Digit" + to_string(digit) + "/" + FileNames[i];
		FILE *in = fopen(tmp.c_str(), "r");
        if (in == NULL) {
            perror(tmp.c_str());
            abort();
        }
        
    	char line[20000];
        char *s[5];
    	while (fgets(line, sizeof line, in) != NULL) {
            if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
			string tt = (string)s[0] + " " + s[1] + " " + s[2] + " " + s[3] + " " + s[4];
			fprintf(out, "%s\n", tt.c_str());
		}
		fclose(in);
		int code;
		do {
			code = remove(tmp.c_str());
		} while (code == -1);

	}
	fclose(out);
}

//Функция, которая разбивает последний полученный файл на мелкий файлы с NumberOfLines в каждом.
static int SplitBigMerge(int dep, string const &Name) {
	string name = "Digit" + to_string(digit) + "/" + Name.c_str();
	FILE *in = fopen(name.c_str(), "r");
    if (in == NULL) {
        perror(name.c_str());
        abort();
    }
    
	int sss = 1;
	int kk = 1;
	string FileOutTm = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(dep) + "Part";
	string FileOut = FileOutTm + to_string(kk);
	FILE *out = fopen(FileOut.c_str(), "w");
    if (out == NULL) {
        perror(FileOut.c_str());
        abort();
    }
    
	char line[20000];
    char *s[5];
	while (fgets(line, sizeof line, in) != NULL) {
        if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
		string tt = (string)s[0] + " " + s[1] + " " + s[2] + " " + s[3] + " " + s[4];
		fprintf(out, "%s\n", tt.c_str());
		if (sss%NumberOfLines == 0) {
			fclose(out);
			kk++;
			FileOut = FileOutTm + to_string(kk);
			out = fopen(FileOut.c_str(), "w");
            if (out == NULL) {
                perror(FileOut.c_str());
                abort();
            }
            
		}
		sss++;
	}
	fclose(out);
	fclose(in);
	int code;
	do {
		code = remove(name.c_str());
	} while (code == -1);
	return kk;
}



//План действий (который работает на математике, должнен работать и тут)
//1) + Сгенерировать все возможные решения, используя 1,...,depth - 1 ( их не будет, если глубина равна 1) - функции IterationToFile и MadeBlocks
//2) + Записать в отдельный файл числа dddd...dd
//3)   Объеденить все полученные файлы в один большой и разбить его на кучу мелких файлов
//4.1) Отсортировать каждый файл и убрать в нём дупликаты
//4.2) Использовать функцию GenerateFacRoot для каждой строчки файла и записать эти строчки обратно
//5) Объеденить все полученные файлы в один большой и разбить его на кучу мелких файлов (прям все, всех порядков), *
//6) Отсортировать и убрать дупликаты, cделать Complement для каждого файла - удаление тех даннах, которые уже были в меньших глубинах
//7) Слить всё в большой файл и разбить его на файлы по 1000 строк.

#if _WIN64
static void collectArg(string const &file, vector<string> &s) {
	WIN32_FIND_DATAA ffd;
	HANDLE ff = ::FindFirstFileA(file.c_str(), &ffd);
    if (ff == INVALID_HANDLE_VALUE) return;
	do {
		if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			string s1 = ffd.cFileName;
			s.push_back(s1);
		}
	} while (::FindNextFileA(ff, &ffd) != 0);
	::FindClose(ff);
}
#else
static void collectArg(string const &file, vector<string> &s) {
	_finddata_t ffd;
	auto ff = _findfirst(file.c_str(), &ffd);
	do {
		if ((ffd.attrib & _A_SUBDIR) == 0) {
			string s1 = ffd.name;
			s.push_back(s1);
		}
	} while (_findnext(ff, &ffd) == 0);
	_findclose(ff);
//    printf("collectArg(%s)=[", file.c_str());
//    for (auto q: s) {
//        printf("%s ", q.c_str());
//    }
//    printf("]\n");
}
#endif

static long long int GetFileSize(std::string filename)
{
	int fd = open(filename.c_str(), O_RDONLY);
	_lseeki64(fd, 0, SEEK_END);
	long long int size = _lseeki64(fd, 0, SEEK_CUR);
	close(fd);
	return size;
}

static vector<int> OneBigIteration(int dep, vector<int> Parts) {
	//1)
	IterationToFIle(Parts, dep);

	//2)
	string fileOut = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(dep) + "Part0";
	string str = to_string(digit);
	for (int i = 1; i <= dep - 1; i++) {
		str += to_string(digit);
	}
	string str2 = str;
	str = str2 + " 1 0 " + to_string(dep) + " (" + str2 + ")";
	FILE *out = fopen(fileOut.c_str(), "w");
    if (out == NULL) {
        perror(fileOut.c_str());
        abort();
    }
    
    if (out == NULL) {
        perror(fileOut.c_str());
        abort();
    }
	fprintf(out, "%s\n", str.c_str());
	fclose(out);

	//3)
	vector<string> files;
	vector<int> filesToDelete;	//Нужно ли обнулять?
	collectArg("Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(dep) + "*", files);
	for (int i = 0; i < files.size(); i++) {
		filesToDelete.push_back(1);
	}
	SplitBigFiles(files, filesToDelete);

	vector<string> SplitAgain;
	collectArg("Digit" + to_string(digit) + "/" + "Splitted*", SplitAgain);
	for (int i = 0; i < SplitAgain.size(); i++) {
		long long int tmp = GetFileSize("Digit" + to_string(digit) + "/" + SplitAgain[i]);
		if (tmp > FILESIZE) {
			SplitBigFile2(SplitAgain[i]);
		}
	}

	//4.1)
	string DD = "Digit" + to_string(digit) + "/";
	vector<string> FileNamesSplitted;
	collectArg(DD + "SplittedFirst*", FileNamesSplitted);
	for (int i = 0; i < FileNamesSplitted.size(); i++) {
		SortFile(DD + FileNamesSplitted[i], DD + FileNamesSplitted[i] + "Sorted");
	}

	//4.2) 
	vector<string> FileNamesSorted;
	collectArg(DD + "SplittedFirst*", FileNamesSorted);
	for (int i = 0; i < FileNamesSorted.size(); i++) {
		GenerateFacRootFile(DD + FileNamesSorted[i], DD + FileNamesSorted[i] + "FacRooted", dep);
	}

	//5)
	vector<string> AllFiles;
	collectArg(DD + "*", AllFiles);
	vector<int> filesToDelete2;						//Нужно ли обнулять?
	for (int i = 0; i < AllFiles.size(); i++) {
		if (AllFiles[i][0] == 'S') {
			filesToDelete2.push_back(1);
		}
		else {
			filesToDelete2.push_back(0);
		}
	}
	SplitBigFiles(AllFiles, filesToDelete2);

	vector<string> SplitAgain2;
	collectArg("Digit" + to_string(digit) + "/" + "Splitted*", SplitAgain2);
	for (int i = 0; i < SplitAgain2.size(); i++) {
		long long int tmp = GetFileSize("Digit" + to_string(digit) + "/" + SplitAgain2[i]);
		if (tmp > FILESIZE) {
			SplitBigFile2(SplitAgain2[i]);
		}
	}

	//6)
	vector<string> FileNamesToComplement;
	collectArg(DD + "SplittedFirst*", FileNamesToComplement);
	for (int i = 0; i < FileNamesToComplement.size(); i++) {
		RemoveDuplicatesAllDepth(DD + FileNamesToComplement[i], DD + FileNamesToComplement[i] + "Complemented", dep);
	}

	//7)
	vector<string> FileNamesComplemented;
	collectArg(DD + "SplittedFirst*", FileNamesComplemented);
	MergeFiles(FileNamesComplemented, DD + "BigMerged");

	int partsNew = SplitBigMerge(dep, "BigMerged");
	Parts.push_back(partsNew);
	return Parts;


}

static void FinalizeData(int dep) {
	for (int i = 1; i <= dep; i++) {
		string DD = "Digit" + to_string(digit) + "/";
		string NameOut = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + to_string(i);
		vector<string> NamesIn;
		collectArg(DD + "Digit" + to_string(digit) + "Depth" + to_string(i) + "Part*", NamesIn);
		MergeFiles(NamesIn, NameOut);
	}
}

static void usage(const char *s) {
    fprintf(stderr, "usage: %s [mode=MODE] [digit=DIGIT] [depth=DEPTH] [BigN=last]\nwhere\n"
        "MODE - one of FIND_ALL_SOLUTIONS, GENERATE_DATA, PROCESS_DATA, FIND_TEST_SOLUTIONS, PRINT_HASH\n"
        "DIGIT - just a digit, 2 by default\n"
        "DEPTH - 7 by default\n"
        "last - 1000 by default\n", s);
    exit(0);
}

//а сколько строк в файле?
static size_t CountFileLines(string const &sOp) {
    FILE *in = fopen(sOp.c_str(), "r");
    if (in == NULL) {
        perror(sOp.c_str());
        abort();
    }

    size_t lines = 0;
    char s[50000];
    while (fgets(s,sizeof s, in) != NULL) {
        lines++;
    }
    fclose(in);
    return lines;
}

//скормить файл хешу
static size_t AddFileToHash(pht &ht, string const &fullName, size_t inplines) {
    clock_t start = clock();
    FILE *in = fopen(fullName.c_str(), "r");
    if (in == NULL) {
        perror(fullName.c_str());
        abort();
    }
    size_t lines = 0;
	char line[20000];
    char *s[5];
	while (fgets(line, sizeof line, in) != NULL) {
        if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 5) break;
        char buf[20000];
        bn t0(s[0]);
        bn t1(s[1]);
        sprintf(buf,"@%s@%s@%s", t0.to_string(16).c_str(), t1.to_string(16).c_str(), s[2]);
        char *p = strchr(s[4],'\n');
        if (p) *p = 0;
        ht.insert(buf, s[4]);
        if (lines % 100000 == 0) {
            fprintf(stderr, "%zu/%zu (%.2lf)%%  \r", lines, inplines, (double)lines*100./inplines);
        }
        lines++;
    }
    fclose(in);
    clock_t end = clock();
    fprintf(stderr,"AddFileToHash(%s) took %.3f seconds\n", fullName.c_str(), (double)(end - start) / CLOCKS_PER_SEC);
    return lines;
}

static size_t CheckHash(pht &ht, string const &fullName, size_t inplines) {
    fprintf(stderr,"\nCheck %s\n", fullName.c_str());
    clock_t start = clock();
    FILE *in = fopen(fullName.c_str(), "r");
    if (in == NULL) {
        perror(fullName.c_str());
        abort();
    }
    size_t lines = 0;
	char line[20000];
    char *s[5];
	while (fgets(line, sizeof line, in) != NULL) {
        if (parse5(line, sizeof line, &s[0], &s[1], &s[2], &s[3], &s[4]) != 50000) break;
        char buf[20000];
        bn t0(s[0]);
        bn t1(s[1]);
        sprintf(buf,"@%s@%s@%s", t0.to_string(16).c_str(), t1.to_string(16).c_str(), s[2]);
        string data;
        if (!ht.find(buf,data)) {
            fprintf(stderr,"Panic: inserted '%s'='%s' not found\n", buf, s[4]);
            fprintf(stderr,"line=%zu\n", lines);
            abort();
        }
        if (data != s[4]) {
            if (data.size() > 0 && data[data.size()-1] == '\n') {
                data.pop_back();
            }
            char *p = strchr(s[4],'\n');
            if (*p) *p = 0;
            fprintf(stderr,"Panic: inserted '%s'='%s' got '%s'\n", buf, s[4], data.c_str());
            fprintf(stderr,"line=%zu\n", lines);
            abort();
        }
        if (lines % 100000 == 0) {
            fprintf(stderr, "%zu/%zu (%.2lf)%%  \r", lines, inplines, (double)lines*100./inplines);
        }
        lines++;
    }
    fclose(in);
    clock_t end = clock();
    fprintf(stderr,"CheckHash(%s) took %.3f seconds\n", fullName.c_str(), (double)(end - start) / CLOCKS_PER_SEC);
    return lines;
}



static void GenerateHashTables(bool checkCreate, bool checkOnly) {
    string fileNamePattern = "Digit" + to_string(digit) + "/Digit" + to_string(digit) + "Depth" + "*.data";
    vector<string> names;
    collectArg(fileNamePattern, names);
    sort(names.begin(), names.end());
    for (auto n : names) {
        size_t inplines = 0;
        if (!checkOnly) {
            //printf("Name: %s: %zu lines\n", n.c_str(), CountFileLines(canonifyName(n)));
            pht ht;
            inplines = CountFileLines(canonifyName(n));
            ht.create(canonifyName(n) + ".hash", inplines*2, inplines*2*8+65536+GetFileSize(canonifyName(n)) / 5 * 6);
            auto lines = AddFileToHash(ht, canonifyName(n), inplines);
        }
        if (checkCreate || checkOnly) {
            pht ht;
            if (inplines == 0) {
                inplines = CountFileLines(canonifyName(n));
            }
            int code = ht.open(canonifyName(n) + ".hash");
            if (code == 0) {
                CheckHash(ht, canonifyName(n), inplines);
            } else {
                fprintf(stderr,"'%s open: code=%d\n", canonifyName(n).c_str(), code);
            }
        }
        printf("%s: added %zu lines\n", canonifyName(n).c_str(), inplines);
    }
}

static void LoadAllHashTables(bool print) {
    string dir = string("Digit") + to_string(digit) + "/";
    vector<string> names;
    collectArg(dir + "*.hash", names);
    sort(names.begin(), names.end());
    for (int i = 0; i < names.size(); i++) {
        printf("Loading %s ", (dir + names[i]).c_str()); fflush(stdout);
        smallSolTriple[i].open(dir + names[i]);
        if (print) {
            smallSolTriple[i].list();
        }
        printf("... done\n");
    }
}


int main(int argc, char **argv) {
    //testbn();  return 0;
    bool checkCreate = false, checkOnly = false;
    clock_t start = clock();
    enum {
        FIND_ALL_SOLUTIONS, GENERATE_DATA, PROCESS_DATA, FIND_TEST_SOLUTIONS, DEBUG, PRINT_HASH,
    } mode = GENERATE_DATA;
    for (int i = 1; i < argc; i++) {
        string s = argv[i];
        if (s.find("mode=") == 0) {
            if (s == "mode=FIND_ALL_SOLUTIONS") {
                mode = FIND_ALL_SOLUTIONS;
            } else if (s == "mode=GENERATE_DATA") {
                mode = GENERATE_DATA;
            } else if (s == "mode=FIND_TEST_SOLUTIONS") {
                mode = FIND_TEST_SOLUTIONS;
            } else if (s == "mode=PROCESS_DATA") {
                mode = PROCESS_DATA;
            } else if (s == "mode=DEBUG") {
                mode = DEBUG;
            } else if (s == "mode=PRINT_HASH") {
                mode = PRINT_HASH;
            } else {
                usage(argv[0]);
            }
        } else if (s.find("digit=") == 0) {
            digit = atoi(&s[6]);
        } else if (s.find("depth=") == 0) {
            depth = atoi(&s[6]);
        } else if (s.find("BigN=") == 0) {
            BigN = atoi(&s[5]);
        } else if (s.find("checkcreate") == 0) {
            checkCreate = true;
        } else if (s.find("checkonly") == 0) {
            checkOnly = true;
        }
    }
    try {

        GenerateModules(569, quadratic569);
        GenerateModules(647, quadratic647);
        GenerateModules(653, quadratic653);
        GenerateModules(659, quadratic659);
        GeneratePowersLimits();
        EmptyArraySol();
        GenerateFactorialTable();

        if (mode == DEBUG) {
            FillArrayOfSolutionsStep1(10 * BigN);
        }
        if (mode == FIND_ALL_SOLUTIONS) {
            LoadAllHashTables(false);
            //UploadAllBeginingsSmallSolTriple(depth);

            FillArrayOfSolutionsStep1(10 * BigN);
            FindStupidSolutionStep2(10 * BigN, depth + 1);
            FindSmartSolutionsStep3(BigN, depth + 1);
            SaveSolutions("Digit" + to_string(digit) + "/Solutions" + to_string(digit), "Digit" + to_string(digit) + "/NumDigits" + to_string(digit));
            FindStupidSolutionStep2(10 * BigN, depth + 2);
            FindSmartSolutionsStep3(BigN, depth + 2);
            SaveSolutions("Digit" + to_string(digit) + "/Solutions" + to_string(digit), "Digit" + to_string(digit) + "/NumDigits" + to_string(digit));
            FindStupidSolutionStep2(10 * BigN, depth + 3);
            FindSmartSolutionsStep3(BigN, depth + 3);
            SaveSolutions("Digit" + to_string(digit) + "/Solutions" + to_string(digit), "Digit" + to_string(digit) + "/NumDigits" + to_string(digit));
            FindStupidSolutionStep2(10 * BigN, depth + 4);
            SaveSolutions("Digit" + to_string(digit) + "/Solutions" + to_string(digit), "Digit" + to_string(digit) + "/NumDigits" + to_string(digit));
        } else if (mode == PRINT_HASH) {
            LoadAllHashTables(true);
        } else if (mode == GENERATE_DATA) {
            clock_t start = clock();

            vector <int> Parts;
            for (int i = 1; i <= depth; i++) {
                Parts = OneBigIteration(i, Parts);
            }
            FinalizeData(depth);

            clock_t end = clock();
            double secs = ((double)end - start) / CLOCKS_PER_SEC;
            printf("%f\n", secs);
        } else if (mode == PROCESS_DATA) {
            GenerateHashTables(checkCreate, checkOnly);
           //
        } else if (mode == FIND_TEST_SOLUTIONS) {
            LoadAllHashTables(false);

            //string sss1 = FindSolutionUsingSmallSol("300 1 0", 5);
            //string sss2 = FindSolutionUsingSmallSol("3045230 1 0", 5);
            bn n1("94109401");
            Triple Tr1 = { 933, 1, 0 };
            Triple Tr2 = { 9701,1,0 };
            Triple Tr3 = { 9703,1,0 };
            clock_t start = clock();
            string tmp1 = FindSmartSolution(Tr1, 5);
            printf("933 = %s\n", tmp1.c_str());
            clock_t end = clock();
            double secs = ((double)end - start) / CLOCKS_PER_SEC;
            printf("%f\n", secs);
            //printf("Number of times we tried to evaluate root_to %d\n", NumberOfEvaluatedRoots);
            //printf("Number of found squares %d\n", NumberOfFoundSquares);
            //printf("Overall number of calls of function IsSquare %d\n", NumberOfCallsOfIsRoot);
            //printf("Number of found simple solutions %d\n", NumberOfSimpleRoots);
        }
	}
	catch (const char *s) {
		printf("Caught '%s'\n", s);
	}
	catch (...) {
		printf("Unhandled exception\n");
	}
    clock_t end = clock();
    printf("total %.3f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
}

//202561911:933 1 0 11 (((((1))+((((1))+(((1))+((1))))!))!)-((((1))+((1)))^{((1))+((11))}))-((11))


